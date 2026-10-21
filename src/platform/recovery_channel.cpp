#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "recovery_channel.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QPointer>
#include <QtEndian>
#include <cstring>
#include <utility>

namespace reword {
namespace {
// Socket permissions restrict the real server's clients. Authenticate the server too:
// a different account can pre-bind a predictable name before Reword starts.
bool sameAccountServer(QLocalSocket &socket) {
#ifdef _WIN32
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &pid))
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    auto identity = [](HANDLE process) {
        std::vector<BYTE> result;
        HANDLE token = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &token))
            return result;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        result.resize(bytes);
        if (!bytes || !GetTokenInformation(token, TokenUser, result.data(), bytes, &bytes))
            result.clear();
        CloseHandle(token);
        return result;
    };
    const auto server = identity(process), current = identity(GetCurrentProcess());
    CloseHandle(process);
    return !server.empty() && !current.empty() &&
           EqualSid(reinterpret_cast<const TOKEN_USER *>(server.data())->User.Sid,
                    reinterpret_cast<const TOKEN_USER *>(current.data())->User.Sid);
#else
    struct ucred peer{};
    socklen_t size = sizeof(peer);
    return ::getsockopt(socket.socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 &&
           size == sizeof(peer) && peer.uid == ::geteuid();
#endif
}
} // namespace
bool recoveryEligible(const QString &a, const QString &b) {
    return !a.isEmpty() && a != b && a.size() <= 2 * MaxPassageCharacters &&
           b.size() <= 2 * MaxPassageCharacters && a.toUcs4().size() <= MaxPassageCharacters &&
           b.toUcs4().size() <= MaxPassageCharacters;
}
QString recoveryEndpoint(const QString &directory) {
    return "reword-recovery-" +
           QString::fromLatin1(
               QCryptographicHash::hash(QFileInfo(directory).absoluteFilePath().toUtf8(),
                                        QCryptographicHash::Sha256)
                   .toHex()
                   .left(24));
}
static qsizetype frameLength(const QByteArray &frame) {
    if (frame.size() < 4)
        return 0;
    if (!frame.startsWith("RWP1"))
        return -1;
    if (frame.size() < 12)
        return 0;
    auto a = qFromLittleEndian<quint32>(frame.constData() + 4);
    auto b = qFromLittleEndian<quint32>(frame.constData() + 8);
    if (!a || a > 2 * MaxPassageCharacters || b > 2 * MaxPassageCharacters)
        return -1;
    return 12 + 2 * qsizetype(a + b);
}
QByteArray encodeRecovery(const QString &a, const QString &b) {
    if (!recoveryEligible(a, b))
        return {};
    QByteArray frame(12 + 2 * (a.size() + b.size()), Qt::Uninitialized);
    std::memcpy(frame.data(), "RWP1", 4);
    qToLittleEndian<quint32>(a.size(), frame.data() + 4);
    qToLittleEndian<quint32>(b.size(), frame.data() + 8);
    qsizetype offset = 12;
    for (const auto &text : {a, b})
        for (QChar unit : text) {
            qToLittleEndian<quint16>(unit.unicode(), frame.data() + offset);
            offset += 2;
        }
    return frame;
}
bool decodeRecovery(const QByteArray &frame, QString &a, QString &b) {
    auto length = frameLength(frame);
    if (length <= 0 || length != frame.size() || length > MaxRecoveryFrame)
        return false;
    auto unitsA = qFromLittleEndian<quint32>(frame.constData() + 4);
    auto unitsB = qFromLittleEndian<quint32>(frame.constData() + 8);
    qsizetype offset = 12;
    auto decode = [&](quint32 units) {
        QString text;
        text.resize(units);
        for (quint32 i = 0; i < units; ++i) {
            text[i] = QChar(qFromLittleEndian<quint16>(frame.constData() + offset));
            offset += 2;
        }
        return text;
    };
    auto original = decode(unitsA), corrected = decode(unitsB);
    if (!recoveryEligible(original, corrected))
        return false;
    a = std::move(original);
    b = std::move(corrected);
    return true;
}
RecoveryServer::RecoveryServer(QObject *parent) : QObject(parent) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(4);
    connect(&server_, &QLocalServer::newConnection, this, &RecoveryServer::accept);
}
RecoveryServer::~RecoveryServer() {
    stop();
}
bool RecoveryServer::listen(const QString &directory) {
    return server_.listen(recoveryEndpoint(directory));
}
void RecoveryServer::stop() {
    server_.close();
    for (auto socket : peers_.keys())
        reject(socket);
}
void RecoveryServer::reject(QLocalSocket *socket) {
    peers_.remove(socket);
    socket->abort();
    socket->deleteLater();
}
void RecoveryServer::accept() {
    while (auto socket = server_.nextPendingConnection()) {
        if (peers_.size() >= 4) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setReadBufferSize(MaxRecoveryFrame + 1);
        auto timer = new QTimer(socket);
        timer->setSingleShot(true);
        timer->setInterval(1000);
        peers_.insert(socket, Peer{{}, timer, false});
        connect(timer, &QTimer::timeout, this, [this, socket] { reject(socket); });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            peers_.remove(socket);
            socket->deleteLater();
        });
        timer->start();
        if (socket->bytesAvailable())
            read(socket);
    }
}
void RecoveryServer::read(QLocalSocket *socket) {
    auto it = peers_.find(socket);
    if (it == peers_.end())
        return;
    auto &peer = it.value();
    peer.buffer += socket->readAll();
    if (!peer.greeted) {
        if (peer.buffer.size() < 4)
            return;
        if (peer.buffer != "RWH1") {
            reject(socket);
            return;
        }
        peer.greeted = true;
        peer.buffer.clear();
        peer.deadline->stop();
        socket->write("RWR1");
        return;
    }
    if (!peer.deadline->isActive())
        peer.deadline->start();
    if (peer.buffer == "RWS1") {
        // Read-only delivery counter for local diagnostics; never return passage text.
        QByteArray status(12, Qt::Uninitialized);
        std::memcpy(status.data(), "STAT", 4);
        qToLittleEndian<quint64>(generation_, status.data() + 4);
        peer.buffer.clear();
        peer.deadline->stop();
        socket->write(status);
        return;
    }
    auto length = frameLength(peer.buffer);
    if (peer.buffer.size() > MaxRecoveryFrame || length < 0) {
        reject(socket);
        return;
    }
    if (!length || peer.buffer.size() < length)
        return;
    QString a, b;
    if (!decodeRecovery(peer.buffer, a, b)) {
        reject(socket);
        return;
    }
    peer.buffer.clear();
    peer.deadline->stop();
    ++generation_;
    QPointer<RecoveryServer> self(this);
    QPointer<QLocalSocket> guarded(socket);
    emit passage(a, b);
    if (self && guarded && peers_.contains(socket))
        socket->write("OKAY");
}
RecoveryClient::RecoveryClient(QObject *parent) : QObject(parent) {
    socket_.setReadBufferSize(16);
    retry_.setInterval(250);
    deadline_.setSingleShot(true);
    deadline_.setInterval(1000);
    connect(&retry_, &QTimer::timeout, this, &RecoveryClient::connectNow);
    connect(&deadline_, &QTimer::timeout, this, &RecoveryClient::reset);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        if (!sameAccountServer(socket_)) {
            reset();
            return;
        }
        stage_ = Stage::Greeting;
        deadline_.start();
        socket_.write("RWH1");
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &RecoveryClient::read);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this] { reset(); });
    connect(&socket_, &QLocalSocket::disconnected, this, &RecoveryClient::reset);
}
RecoveryClient::~RecoveryClient() {
    blockSignals(true); // Captured owner state may already have been destroyed.
    stop();
}
void RecoveryClient::start(const QString &directory) {
    QPointer<RecoveryClient> self(this);
    stop();
    if (!self)
        return;
    endpoint_ = recoveryEndpoint(directory);
    running_ = true;
    retry_.start();
    connectNow();
}
void RecoveryClient::stop() {
    running_ = false;
    retry_.stop();
    reset();
}
bool RecoveryClient::active() const {
    return socket_.state() == QLocalSocket::ConnectedState &&
           (stage_ == Stage::Ready || stage_ == Stage::AwaitingAck);
}
bool RecoveryClient::ready() const {
    return active() && stage_ == Stage::Ready && current_.isEmpty() && pending_.isEmpty();
}
void RecoveryClient::connectNow() {
    if (!running_ || socket_.state() != QLocalSocket::UnconnectedState)
        return;
    stage_ = Stage::Connecting;
    deadline_.start();
    socket_.connectToServer(endpoint_);
}
void RecoveryClient::reset() {
    if (stage_ == Stage::None)
        return;
    const bool wasActive = stage_ == Stage::Ready || stage_ == Stage::AwaitingAck;
    const bool lostPassage = !current_.isEmpty() || !pending_.isEmpty();
    stage_ = Stage::None; // Guard against synchronous error/disconnected signals from abort().
    deadline_.stop();
    response_.clear();
    current_.clear();
    pending_.clear();
    socket_.abort();
    QPointer<RecoveryClient> self(this);
    if (wasActive)
        emit activeChanged(false);
    if (self && lostPassage)
        emit deliveryFailed();
}
bool RecoveryClient::write(QByteArray frame) {
    current_ = std::move(frame);
    stage_ = Stage::AwaitingAck;
    deadline_.start();
    if (socket_.write(current_) < 0) {
        reset();
        return false;
    }
    return true;
}
bool RecoveryClient::submit(const QString &a, const QString &b) {
    if (!active())
        return false;
    auto frame = encodeRecovery(a, b);
    if (frame.isEmpty())
        return false;
    if (stage_ == Stage::AwaitingAck) {
        pending_ = std::move(frame);
        return true;
    }
    pending_
        .clear(); // A new request supersedes an older queued one, including reentrant callbacks.
    return write(std::move(frame));
}
void RecoveryClient::read() {
    response_ += socket_.readAll();
    if (response_.size() < 4)
        return;
    if (response_.size() != 4) {
        reset();
        return;
    }
    if (stage_ == Stage::Greeting && response_ == "RWR1") {
        response_.clear();
        deadline_.stop();
        stage_ = Stage::Ready;
        emit activeChanged(true);
    } else if (stage_ == Stage::AwaitingAck && response_ == "OKAY") {
        response_.clear();
        current_.clear();
        deadline_.stop();
        stage_ = Stage::Ready;
        QPointer<RecoveryClient> self(this);
        emit delivered();
        if (self && stage_ == Stage::Ready && active() && !pending_.isEmpty()) {
            auto frame = std::exchange(pending_, {});
            write(std::move(frame));
        }
    } else
        reset();
}
} // namespace reword
