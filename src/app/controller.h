#pragma once
#include "service.h"
#include <QThread>
#include <QSystemTrayIcon>
#include <QVariantMap>
namespace reword {
class RecoveryServer;
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY updated)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY updated)
    Q_PROPERTY(QVariantList history READ history NOTIFY updated)
    Q_PROPERTY(QVariantList insights READ insights NOTIFY updated)
    Q_PROPERTY(QStringList words READ words NOTIFY updated)
    Q_PROPERTY(QVariantList replacements READ replacements NOTIFY updated)
    Q_PROPERTY(QString status READ status NOTIFY updated)
    Q_PROPERTY(QString platform READ platform NOTIFY updated)
    Q_PROPERTY(QString notice READ notice NOTIFY updated)
    Q_PROPERTY(QString recoveryOriginal READ recoveryOriginal NOTIFY updated)
    Q_PROPERTY(QString recoveryCorrected READ recoveryCorrected NOTIFY updated)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY updated)
    Q_PROPERTY(QString pendingName READ pendingName NOTIFY updated)
    Q_PROPERTY(bool busy READ busy NOTIFY updated)
    Q_PROPERTY(bool dark READ dark NOTIFY updated)
    Q_PROPERTY(bool paused READ paused NOTIFY updated)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Store store_;
    RecoveryServer *recoveryServer_ = nullptr;
    QThread worker_;
    Service *service_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QString platform_ = "Checking desktop integration…", notice_, pendingId_, pendingName_;
    QVariantList queue_;
    QString recoveryOriginal_, recoveryCorrected_;
    bool busy_ = false, serviceBusy_ = false, fcitxBusy_ = false, shortcuts_ = false;
    mutable bool storageFailed_ = false;
    template <class F> auto read(F getter) const {
        try {
            return getter();
        } catch (...) {
            storageFailed_ = true;
            return decltype(getter()){};
        }
    }
    template <class F> void guard(F work) {
        storageFailed_ = false;
        try {
            work();
        } catch (...) {
            storageFailed_ = true;
        }
        if (storageFailed_) {
            notice_ = "Local storage could not be updated. Try again after checking available "
                      "space and access.";
            emit updated();
        }
    }
    QTimer refresh_;
    void refreshTray();
    void nextApplication();
    bool rememberPassage(const QString &original, const QString &corrected);

  public:
    explicit Controller(const QString &directory, QObject *parent = nullptr,
                        std::unique_ptr<Adapter> adapter = {});
    ~Controller();
    QVariantMap settings() const;
    QVariantList apps() const {
        return read([&] { return store_.applications(); });
    }
    QVariantList history() const {
        return read([&] { return store_.histories(false); });
    }
    QVariantList insights() const {
        return read([&] { return store_.statistics(); });
    }
    QStringList words() const {
        return read([&] {
            auto w = store_.words().values();
            w.sort();
            return w;
        });
    }
    QVariantList replacements() const;
    QString status() const {
        if (storageFailed_)
            return "Storage unavailable";
        try {
            return store_.paused() ? "Paused" : "Ready";
        } catch (...) {
            storageFailed_ = true;
            return "Storage unavailable";
        }
    }
    QString platform() const {
        return platform_;
    }
    QString notice() const {
        return notice_.isEmpty() && storageFailed_
                   ? "Local storage is unavailable. Changes are paused until access returns."
                   : notice_;
    }
    QString pendingName() const {
        return pendingName_;
    }
    bool busy() const {
        return busy_ || serviceBusy_ || fcitxBusy_;
    }
    bool dark() const {
        return read([&] { return store_.value("dark", true).toBool(); });
    }
    bool paused() const {
        try {
            return store_.paused();
        } catch (...) {
            storageFailed_ = true;
            return true;
        }
    }
    bool trayAvailable() const {
        return tray_ != nullptr;
    }
    Q_INVOKABLE int characterCount(const QString &text) const;
    Q_INVOKABLE void rejectOversizedText() {
        notice_ = "Text exceeds the workspace limit. The previous text was kept.";
        emit updated();
    }
    Q_INVOKABLE void set(QString key, QVariant value);
    Q_INVOKABLE void decide(QString id, QString decision);
    Q_INVOKABLE void consent(QString decision);
    Q_INVOKABLE void pause(int minutes);
    Q_INVOKABLE void fix(QString text);
    Q_INVOKABLE void cancel();
    QString recoveryOriginal() const {
        return recoveryOriginal_;
    }
    QString recoveryCorrected() const {
        return recoveryCorrected_;
    }
    bool recoveryAvailable() const {
        return !recoveryOriginal_.isEmpty();
    }
    Q_INVOKABLE void clearRecovery();
    Q_INVOKABLE void copyRecoveryOriginal();
    Q_INVOKABLE void recordApplied(QString original, QString corrected);
    Q_INVOKABLE void copy(QString text);
    Q_INVOKABLE QVariantMap historyDetail(qint64 id) const {
        return read([&] { return store_.historyDetail(id); });
    }
    Q_INVOKABLE void deleteHistory(qint64 id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void clearStatistics();
    Q_INVOKABLE void addWord(QString word);
    Q_INVOKABLE void removeWord(QString word);
    Q_INVOKABLE void replacement(QString original, QString corrected);
    Q_INVOKABLE void clearNotice() {
        notice_.clear();
        emit updated();
    }
    Q_INVOKABLE void quit();
  signals:
    void updated();
    void showWindow();
    void pauseDialog();
    void correctedText(QString original, QString corrected);
    void recoveryCleared();
};
} // namespace reword
