#pragma once
#include "core.h"
#include <QSqlDatabase>
#include <QVariantList>
namespace reword {
class Store {
    QSqlDatabase db_;
    QString connection_;
    void exec(const QString &sql);

  public:
    explicit Store(const QString &directory);
    ~Store();
    QVariant value(const QString &, const QVariant &fallback = {}) const;
    void setValue(const QString &, const QVariant &);
    Options options() const;
    void discover(const QString &id, const QString &name, bool restricted = false);
    void decide(const QString &id, const QString &decision, QDate today = QDate::currentDate());
    bool allowed(const QString &) const;
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
