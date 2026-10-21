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
    auto it = peers_.find(socket);
    const bool operation = it != peers_.end() && it->operation;
    if (it != peers_.end())
        peers_.erase(it);
    socket->abort();
    socket->deleteLater();
    if (operation && --operations_ == 0)
        emit operationActive(false);
}
void RecoveryServer::setOperation(QLocalSocket *socket, bool active) {
    auto it = peers_.find(socket);
    if (it == peers_.end() || it->operation == active)
        return;
    it->operation = active;
    if (active) {
        if (operations_++ == 0)
            emit operationActive(true);
    } else if (--operations_ == 0)
        emit operationActive(false);
}
void RecoveryServer::cancelOperations() {
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
        if (it->greeted && it->operation)
            it.key()->write("CNCL");
}
void RecoveryServer::accept() {
    while (auto socket = server_.nextPendingConnection()) {
        if (peers_.size() >= 4) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setReadBufferSize(MaxRecoveryFrame + 16);
        auto timer = new QTimer(socket);
        timer->setSingleShot(true);
        timer->setInterval(1000);
        peers_.insert(socket, Peer{{}, timer, false});
        connect(timer, &QTimer::timeout, this, [this, socket] { reject(socket); });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (peers_.contains(socket))
                reject(socket);
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
    it->buffer += socket->readAll();
    while (peers_.contains(socket)) {
        it = peers_.find(socket);
        auto &peer = it.value();
        if (peer.buffer.size() > MaxRecoveryFrame + 16) {
            reject(socket);
            return;
        }
        if (peer.buffer.size() < 4) {
            if (!peer.buffer.isEmpty() && !peer.deadline->isActive())
                peer.deadline->start();
            return;
        }
        const auto tag = peer.buffer.left(4);
        if (!peer.greeted) {
            if (tag != "RWH1") {
                reject(socket);
                return;
            }
            peer.greeted = true;
            peer.buffer.remove(0, 4);
            peer.deadline->stop();
            socket->write("RWR1");
            continue;
        }
        if (!peer.deadline->isActive())
            peer.deadline->start();
        if (tag == "RWB1" || tag == "RWE1") {
            peer.buffer.remove(0, 4);
            QPointer<RecoveryServer> self(this);
            setOperation(socket, tag == "RWB1");
            if (!self)
                return;
        } else if (tag == "RWS1") {
            // Read-only delivery counter for local diagnostics; never return passage text.
            peer.buffer.remove(0, 4);
            QByteArray status(12, Qt::Uninitialized);
            std::memcpy(status.data(), "STAT", 4);
            qToLittleEndian<quint64>(generation_, status.data() + 4);
            socket->write(status);
        } else if (tag == "RWP1") {
            auto length = frameLength(peer.buffer);
            if (length < 0) {
                reject(socket);
                return;
            }
            if (!length || peer.buffer.size() < length)
                return;
            if (peer.buffer.size() > length && peer.buffer.size() < length + 4)
                return; // Validate a trailing command before delivering this passage.
            if (peer.buffer.size() >= length + 4) {
                const auto next = peer.buffer.mid(length, 4);
                if (next != "RWB1" && next != "RWE1" && next != "RWS1" && next != "RWP1") {
                    reject(socket);
                    return;
                }
            }
            const auto frame = peer.buffer.left(length);
            peer.buffer.remove(0, length);
            QString a, b;
            if (!decodeRecovery(frame, a, b)) {
                reject(socket);
                return;
            }
            ++generation_;
            QPointer<RecoveryServer> self(this);
            QPointer<QLocalSocket> guarded(socket);
            emit passage(a, b);
            if (!self || !guarded || !peers_.contains(socket))
                return;
            socket->write("OKAY");
        } else {
            reject(socket);
            return;
        }
        if (peers_.contains(socket)) {
            auto &current = peers_[socket];
            if (current.buffer.isEmpty())
                current.deadline->stop();
        }
    }
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
    operation_ = false;
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
bool RecoveryClient::beginOperation() {
    if (!ready() || operation_)
        return false;
    operation_ = true;
    if (socket_.write("RWB1") < 0) {
        reset();
        return false;
    }
    return true;
}
bool RecoveryClient::endOperation() {
    if (!operation_)
        return false;
    operation_ = false;
    if (!active() || socket_.write("RWE1") < 0) {
        reset();
        return false;
    }
    return true;
}
void RecoveryClient::read() {
    response_ += socket_.readAll();
    if (response_.size() > 64) {
        reset();
        return;
    }
    if (!response_.isEmpty() && !deadline_.isActive())
        deadline_.start();
    while (response_.size() >= 4) {
        const auto token = response_.left(4);
        response_.remove(0, 4);
        if (token == "CNCL" &&
            (stage_ == Stage::Ready || stage_ == Stage::AwaitingAck)) {
            if (operation_) {
                QPointer<RecoveryClient> self(this);
                emit cancelRequested();
                if (!self)
                    return;
            }
        } else if (stage_ == Stage::Greeting && token == "RWR1") {
            deadline_.stop();
            stage_ = Stage::Ready;
            QPointer<RecoveryClient> self(this);
            emit activeChanged(true);
            if (!self)
                return;
        } else if (stage_ == Stage::AwaitingAck && token == "OKAY") {
            current_.clear();
            deadline_.stop();
            stage_ = Stage::Ready;
            QPointer<RecoveryClient> self(this);
            emit delivered();
            if (!self)
                return;
            if (stage_ == Stage::Ready && active() && !pending_.isEmpty()) {
                auto frame = std::exchange(pending_, {});
                write(std::move(frame));
            }
        } else {
            reset();
            return;
        }
    }
    if (response_.isEmpty() && stage_ == Stage::Ready)
        deadline_.stop();
}
} // namespace reword
