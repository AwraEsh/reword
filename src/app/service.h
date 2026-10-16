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
    QString directory_;
    std::unique_ptr<Store> store_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Adapter> adapter_;
    QTimer *timer_ = nullptr;
    Field previous_, undoAfter_;
    QString undoBefore_;
    int undoCaret_ = 0;
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
    bool wayland_ = false, jobRunning_ = false;
    QFutureWatcher<Change> *job_ = nullptr;
    std::unique_ptr<QLockFile> jobLock_;
    void runText(QString input, bool native, Field field = {});
    void tick();
    Field permitted();
    void fixSelection();
    void undo();

  public:
    explicit Service(QString directory, std::unique_ptr<Adapter> adapter = {})
        : directory_(std::move(directory)), adapter_(std::move(adapter)) {}
    void cancel() {
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
    void application(QString id, QString name);
    void result(QString original, QString corrected, QString error, bool native);
    void message(QString message);
    void state(QString description, bool shortcutsOK);
    void corrected(QString original, QString corrected);
};
} // namespace reword
