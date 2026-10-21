#pragma once
#include "platform/adapter.h"
#include "core/store.h"
#include <QObject>
#include <QTimer>
#include <QFutureWatcher>
#include <QLockFile>
namespace reword {
class Service : public QObject {
    Q_OBJECT
    friend struct ServiceTestAccess;
    QString directory_;
    std::unique_ptr<Store> store_;
    std::unique_ptr<Engine> engine_;
    // Used only by one passage job at a time; never shared with live correction.
    std::unique_ptr<Engine> passageEngine_;
    std::unique_ptr<Adapter> adapter_;
    QTimer *timer_ = nullptr;
    Field previous_, undoAfter_;
    QString undoBefore_;
    int undoCaret_ = 0;
    bool undoPassage_ = false;
    RetryCycle retry_;
    QString retryPrefix_, retryWord_;
    int retryStart_ = -1;
    QString lastAsk_;
    QDate askDate_;
    QStringList shortcutSettings_, workingShortcuts_;
    QString lastDescription_;
    qint64 platformPoll_ = 0, shortcutRevision_ = -1;
    bool shortcutsHealthy_ = false;
    std::atomic_bool cancel_{false};
    std::atomic<quint64> cancellationRevision_{0};
    bool wayland_ = false, jobRunning_ = false, nativeJob_ = false;
    // Metadata only: never duplicate a native job's text in its target guard.
    QString nativeJobApp_, nativeJobField_;
    QFutureWatcher<Change> *job_ = nullptr;
    std::unique_ptr<QLockFile> jobLock_;
    void clearJobTarget();
    void cancelNativeJob();
    void resetRetry();
    void resetObservation();
    void runText(QString input, bool native, Field field = {});
    void tick();
    Field permitted(const Field *expected = nullptr);
    bool policyCurrent(const Field &field, qint64 revision, quint64 cancellation);
    void fixSelection();
    void undo();

  public:
    explicit Service(QString directory, std::unique_ptr<Adapter> adapter = {})
        : directory_(std::move(directory)), adapter_(std::move(adapter)) {}
    void cancel() {
        cancellationRevision_.fetch_add(1);
        cancel_.store(true);
    }
    void prepare() {
        cancel_.store(false);
    }
  public slots:
    void start();
    void stop();
    void processText(QString input);
    void action(QString name);
  signals:
    void operationActive(bool active);
    void application(QString id, QString name);
    void result(QString original, QString corrected, QString error, bool native);
    void message(QString message);
    void state(QString description, bool shortcutsOK);
    void corrected(QString original, QString corrected);
    void passageRecoveryAvailable(QString original, QString corrected);
};
} // namespace reword
