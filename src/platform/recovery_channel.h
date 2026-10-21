#pragma once
#include "core/core.h"
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace reword {
inline constexpr qsizetype MaxRecoveryFrame = 12 + 8 * MaxPassageCharacters;
bool recoveryEligible(const QString &original, const QString &corrected);
QString recoveryEndpoint(const QString &directory);
QByteArray encodeRecovery(const QString &original, const QString &corrected);
bool decodeRecovery(const QByteArray &frame, QString &original, QString &corrected);

class RecoveryServer final : public QObject {
    Q_OBJECT
    struct Peer {
        QByteArray buffer;
        QTimer *deadline = nullptr;
        bool greeted = false;
        bool operation = false;
    };
    QLocalServer server_;
    QHash<QLocalSocket *, Peer> peers_;
    quint64 generation_ = 0;
    int operations_ = 0;
    void accept();
    void read(QLocalSocket *socket);
    void reject(QLocalSocket *socket);
    void setOperation(QLocalSocket *socket, bool active);

  public:
    explicit RecoveryServer(QObject *parent = nullptr);
    ~RecoveryServer() override;
    bool listen(const QString &directory);
    void stop();
    int clientCount() const {
        return peers_.size();
    }
    QString fullServerName() const {
        return server_.fullServerName();
    }
    void cancelOperations();
  signals:
    void passage(QString original, QString corrected);
    void operationActive(bool active);
};

class RecoveryClient final : public QObject {
    Q_OBJECT
    enum class Stage { None, Connecting, Greeting, Ready, AwaitingAck };
    QLocalSocket socket_;
    QTimer retry_, deadline_;
    QString endpoint_;
    QByteArray response_, current_, pending_;
    Stage stage_ = Stage::None;
    bool running_ = false, operation_ = false;
    void connectNow();
    void read();
    void reset();
    bool write(QByteArray frame);

  public:
    explicit RecoveryClient(QObject *parent = nullptr);
    ~RecoveryClient() override;
    void start(const QString &directory);
    void stop();
    bool active() const;
    // No older frame can still acknowledge a different operation.
    bool ready() const;
    bool submit(const QString &original, const QString &corrected);
    bool beginOperation();
    bool endOperation();
  signals:
    void activeChanged(bool active);
    void delivered();
    void deliveryFailed();
    void cancelRequested();
};
} // namespace reword
