#pragma once
#include "core.h"
#include <QSqlDatabase>
#include <QVariantList>
class QSqlQuery;
namespace reword {
enum class ApplicationMode { Disabled, Automatic, ShortcutOnly };
struct ShortcutSettings {
    QStringList keys;
    qint64 revision = 0;
};

class Store {
    QSqlDatabase db_;
    QString connection_;
    void exec(const QString &sql);
    void updateSetting(const QString &, const QVariant &, bool invalidateCorrection);
    void changeCorrectionConfiguration(QSqlQuery &query);

  public:
    explicit Store(const QString &directory, int busyTimeoutMs = 1000);
    ~Store();
    QVariant value(const QString &, const QVariant &fallback = {}) const;
    void setValue(const QString &, const QVariant &);
    void setCorrectionOption(const QString &, const QVariant &);
    Options options() const;
    ShortcutSettings shortcutSettings() const;
    bool restoreShortcuts(const QStringList &expected, qint64 revision,
                          const QStringList &previous);
    void discover(const QString &id, const QString &name, bool restricted = false);
    void decide(const QString &id, const QString &decision, QDate today = QDate::currentDate());
    void removeApplication(const QString &id);
    ApplicationMode applicationMode(const QString &id) const;
    bool allowed(const QString &id) const;
    bool selectionAllowed(const QString &id) const;
    bool shouldAsk(const QString &, QDate today = QDate::currentDate()) const;
    QVariantList applications() const;
    void history(const QString &original, const QString &corrected, const Options &,
                 qint64 now = QDateTime::currentSecsSinceEpoch());
    void prune(const Options &, qint64 now = QDateTime::currentSecsSinceEpoch());
    QVariantList histories(bool full = true) const;
    QVariantMap historyDetail(qint64 id) const;
    void deleteHistory(qint64 id);
    void clearHistory();
    void count(const QString &, const QString &, const Options &);
    QVariantList statistics() const;
    void clearStatistics();
    void addWord(const QString &);
    void removeWord(const QString &);
    QSet<QString> words() const;
    void replacement(const QString &, const QString &);
    QHash<QString, QString> replacements() const;
    bool paused(qint64 now = QDateTime::currentSecsSinceEpoch()) const;
    void pause(int minutes);
};
} // namespace reword
