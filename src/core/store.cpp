#include "store.h"
#include "private_storage.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace reword {
static const QStringList shortcutKeys{"fixShortcut", "undoShortcut", "pauseShortcut"};
static void checked(QSqlQuery &q) {
    if (!q.exec())
        throw std::runtime_error("Local database operation failed.");
}
static QVariant decodeSetting(const QByteArray &json, const QVariant &fallback) {
    const auto values = QJsonDocument::fromJson(json).array();
    return values.isEmpty() ? fallback : values.first().toVariant();
}
static bool pauseActive(qint64 until, qint64 now) {
    return until == -1 || until > now;
}
void Store::exec(const QString &sql) {
    QSqlQuery q(db_);
    q.prepare(sql);
    checked(q);
}
Store::Store(const QString &directory, int busyTimeoutMs) {
    validateStorageLocation(directory);
    if (QFileInfo(directory).isSymLink())
        throw std::runtime_error("Unsafe data directory.");
    if (!QDir().mkpath(directory))
        throw std::runtime_error("Cannot create private data directory.");
    auto path = directory + "/reword.sqlite";
    if (QFileInfo(path).isSymLink())
        throw std::runtime_error("Unsafe database path.");
    for (const auto &suffix : {QString{}, QString("-journal"), QString("-wal"), QString("-shm")}) {
        auto candidate = path + suffix;
        if (QFileInfo(candidate).exists() || QFileInfo(candidate).isSymLink())
            validateExistingStorageFile(candidate);
    }
    restrictStoragePath(directory, true);
    for (const auto &suffix : {QString{}, QString("-journal"), QString("-wal"), QString("-shm")}) {
        auto candidate = path + suffix;
        if (QFileInfo(candidate).exists())
            restrictStoragePath(candidate, false);
    }
    connection_ = QUuid::createUuid().toString();
    try {
        db_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
        db_.setDatabaseName(path);
        if (!db_.open())
            throw std::runtime_error("Cannot open local database.");
        restrictStoragePath(path, false);
        exec(QString("PRAGMA busy_timeout=%1").arg(std::clamp(busyTimeoutMs, 0, 1000)));
        exec("PRAGMA secure_delete=ON");
        exec("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
        exec("CREATE TABLE IF NOT EXISTS applications(id TEXT PRIMARY KEY,name TEXT NOT "
             "NULL,decision "
             "TEXT NOT NULL DEFAULT 'unknown',remind TEXT)");
        exec("CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY,created INTEGER NOT "
             "NULL,original TEXT NOT NULL,corrected TEXT NOT NULL,bytes INTEGER NOT NULL)");
        exec("CREATE TABLE IF NOT EXISTS statistics(original TEXT NOT NULL,corrected TEXT NOT "
             "NULL,count INTEGER NOT NULL,PRIMARY KEY(original,corrected))");
        exec("CREATE TABLE IF NOT EXISTS words(word TEXT PRIMARY KEY)");
        exec("CREATE TABLE IF NOT EXISTS replacements(original TEXT PRIMARY KEY,corrected TEXT NOT "
             "NULL)");
        prune(options());
    } catch (...) {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_);
        throw;
    }
}
Store::~Store() {
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_);
}
QVariant Store::value(const QString &key, const QVariant &fallback) const {
    QSqlQuery q(db_);
    q.prepare("SELECT value FROM settings WHERE key=?");
    q.addBindValue(key);
    checked(q);
    if (!q.next())
        return fallback;
    return decodeSetting(q.value(0).toByteArray(), fallback);
}
void Store::setValue(const QString &key, const QVariant &value) {
    updateSetting(key, value, false);
}
void Store::setCorrectionOption(const QString &key, const QVariant &value) {
    updateSetting(key, value, true);
}
void Store::updateSetting(const QString &key, const QVariant &value, bool invalidateCorrection) {
    QSqlQuery q(db_);
    q.prepare(
        "INSERT INTO settings VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value "
        "WHERE settings.value<>excluded.value");
    q.addBindValue(key);
    q.addBindValue(QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList({value})).toJson(QJsonDocument::Compact)));
    if (invalidateCorrection)
        changeCorrectionConfiguration(q);
    else
        checked(q);
}
ShortcutSettings Store::shortcutSettings() const {
    ShortcutSettings settings{{"Ctrl+Alt+R", "Ctrl+Alt+Z", "Ctrl+Alt+Shift+P"}};
    QSqlQuery query(db_);
    query.prepare("SELECT key,value FROM settings WHERE key IN "
                  "('fixShortcut','undoShortcut','pauseShortcut','configRevision')");
    checked(query);
    while (query.next()) {
        const auto key = query.value(0).toString();
        const auto array = QJsonDocument::fromJson(query.value(1).toByteArray()).array();
        if (array.isEmpty())
            continue;
        const auto value = array.first().toVariant();
        if (key == "configRevision")
            settings.revision = value.toLongLong();
        else if (const auto index = shortcutKeys.indexOf(key); index >= 0)
            settings.keys[index] = value.toString();
    }
    return settings;
}
bool Store::restoreShortcuts(const QStringList &expected, qint64 revision,
                             const QStringList &previous) {
    if (expected.size() != 3 || previous.size() != 3)
        throw std::invalid_argument("Invalid shortcut configuration.");
    if (!db_.transaction())
        throw std::runtime_error("Cannot begin shortcut settings transaction.");
    try {
        const auto settings = shortcutSettings();
        if (settings.keys != expected || settings.revision != revision) {
            db_.rollback();
            return false;
        }
        for (int index = 0; index < shortcutKeys.size(); ++index)
            setValue(shortcutKeys[index], previous[index]);
        setValue("configRevision", revision + 1);
        if (!db_.commit())
            throw std::runtime_error("Cannot restore shortcut settings.");
        return true;
    } catch (...) {
        db_.rollback();
        throw;
    }
}
Options Store::options() const {
    QHash<QString, QVariant> settings;
    QSqlQuery q(db_);
    q.prepare("SELECT key,value FROM settings WHERE key IN ("
              "'english','persian','spelling','abbreviations','capitalization',"
              "'normalizePersian','halfSpace','periods','commas','spacing',"
              "'paragraphPeriods','introductoryCommas','questionMarks','exclamationMarks',"
              "'semicolons','colons','history','statistics','notifications','browsers',"
              "'sensitivity','historyCount','historyMiB','historyDays','maxCharacters')");
    checked(q);
    while (q.next()) {
        const auto array = QJsonDocument::fromJson(q.value(1).toByteArray()).array();
        if (!array.isEmpty())
            settings.insert(q.value(0).toString(), array.first().toVariant());
    }
    // Fresh on every call: policy changes from other connections remain visible.
    const auto value = [&settings](const QString &key, const QVariant &fallback) {
        return settings.value(key, fallback);
    };
    Options o;
#define BOOL(k) o.k = value(#k, o.k).toBool()
    BOOL(english);
    BOOL(persian);
    BOOL(spelling);
    BOOL(abbreviations);
    BOOL(capitalization);
    BOOL(normalizePersian);
    BOOL(halfSpace);
    BOOL(periods);
    BOOL(commas);
    BOOL(spacing);
    BOOL(paragraphPeriods);
    BOOL(introductoryCommas);
    BOOL(questionMarks);
    BOOL(exclamationMarks);
    BOOL(semicolons);
    BOOL(colons);
    BOOL(history);
    BOOL(statistics);
    BOOL(notifications);
    BOOL(browsers);
#undef BOOL
    o.sensitivity = std::clamp(value("sensitivity", 0).toInt(), 0, 1);
    o.historyCount = std::clamp(value("historyCount", 100).toInt(), 1, 500);
    o.historyMiB = std::clamp(value("historyMiB", 20).toInt(), 1, 100);
    o.historyDays = std::clamp(value("historyDays", 30).toInt(), 1, 365);
    o.maxCharacters =
        std::clamp(value("maxCharacters", MaxPassageCharacters).toInt(), 100, MaxPassageCharacters);
    return o;
}
void Store::discover(const QString &id, const QString &name, bool restricted) {
    if (id.isEmpty() || id.size() > 4096)
        return;
    QSqlQuery q(db_);
    q.prepare("INSERT OR IGNORE INTO applications(id,name,decision) VALUES(?,?,?)");
    q.addBindValue(id);
    q.addBindValue(name.left(200));
    q.addBindValue(restricted ? "deny" : "unknown");
    checked(q);
}
void Store::decide(const QString &id, const QString &decision, QDate today) {
    if (decision != "allow" && decision != "shortcut" && decision != "deny" && decision != "later")
        return;
    const auto remind = decision == "later" ? today.addDays(1).toString(Qt::ISODate) : QString("");
    QSqlQuery q(db_);
    q.prepare("UPDATE applications SET decision=?,remind=? WHERE id=? "
              "AND (decision<>? OR COALESCE(remind,'')<>?)");
    q.addBindValue(decision);
    q.addBindValue(remind);
    q.addBindValue(id);
    q.addBindValue(decision);
    q.addBindValue(remind);
    changeCorrectionConfiguration(q);
}
void Store::removeApplication(const QString &id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM applications WHERE id=?");
    q.addBindValue(id);
    changeCorrectionConfiguration(q);
}
void Store::changeCorrectionConfiguration(QSqlQuery &q) {
    if (!db_.transaction())
        throw std::runtime_error("Cannot begin correction configuration transaction.");
    try {
        checked(q);
        if (q.numRowsAffected() > 0)
            setValue("configRevision", value("configRevision", 0).toLongLong() + 1);
        if (!db_.commit())
            throw std::runtime_error("Cannot commit correction configuration transaction.");
    } catch (...) {
        db_.rollback();
        throw;
    }
}
static ApplicationMode modeForDecision(const QString &decision) {
    if (decision == "allow")
        return ApplicationMode::Automatic;
    if (decision == "shortcut")
        return ApplicationMode::ShortcutOnly;
    return ApplicationMode::Disabled;
}
ApplicationMode Store::applicationMode(const QString &id) const {
    QSqlQuery q(db_);
    q.prepare("SELECT decision FROM applications WHERE id=?");
    q.addBindValue(id);
    checked(q);
    return q.next() ? modeForDecision(q.value(0).toString()) : ApplicationMode::Disabled;
}
NativePolicy Store::nativePolicy(const QString &id, qint64 now) const {
    // All four values come from the same SQLite snapshot, including the revision.
    QSqlQuery q(db_);
    q.prepare("SELECT (SELECT decision FROM applications WHERE id=?),"
              "(SELECT value FROM settings WHERE key='pauseUntil'),"
              "(SELECT value FROM settings WHERE key='browsers'),"
              "(SELECT value FROM settings WHERE key='configRevision')");
    q.addBindValue(id);
    checked(q);
    if (!q.next())
        throw std::runtime_error("Cannot read native correction policy.");
    return {modeForDecision(q.value(0).toString()),
            pauseActive(decodeSetting(q.value(1).toByteArray(), 0).toLongLong(), now),
            decodeSetting(q.value(2).toByteArray(), Options{}.browsers).toBool(),
            decodeSetting(q.value(3).toByteArray(), 0).toLongLong()};
}
bool Store::allowed(const QString &id) const {
    return applicationMode(id) == ApplicationMode::Automatic;
}
bool Store::selectionAllowed(const QString &id) const {
    return applicationMode(id) != ApplicationMode::Disabled;
}
bool Store::shouldAsk(const QString &id, QDate today) const {
    QSqlQuery q(db_);
    q.prepare("SELECT decision,remind FROM applications WHERE id=?");
    q.addBindValue(id);
    checked(q);
    if (!q.next())
        return false;
    return q.value(0) == "unknown" ||
           (q.value(0) == "later" &&
            QDate::fromString(q.value(1).toString(), Qt::ISODate) <= today);
}
QVariantList Store::applications() const {
    QVariantList rows;
    QSqlQuery q("SELECT id,name,decision,remind FROM applications ORDER BY lower(name)", db_);
    while (q.next())
        rows << QVariantMap{{"id", q.value(0)},
                            {"name", q.value(1)},
                            {"decision", q.value(2)},
                            {"remind", q.value(3)}};
    return rows;
}
void Store::history(const QString &a, const QString &b, const Options &o, qint64 now) {
    if (!o.history || a == b)
        return;
    qint64 bytes = a.toUtf8().size() + b.toUtf8().size();
    if (bytes > qint64(o.historyMiB) * 1024 * 1024)
        return;
    if (!db_.transaction())
        throw std::runtime_error("Cannot begin history transaction.");
    try {
        QSqlQuery q(db_);
        q.prepare("INSERT INTO history(created,original,corrected,bytes) VALUES(?,?,?,?)");
        q.addBindValue(now);
        q.addBindValue(a);
        q.addBindValue(b);
        q.addBindValue(bytes);
        checked(q);
        prune(o, now);
        if (!db_.commit())
            throw std::runtime_error("Cannot commit history.");
    } catch (...) {
        db_.rollback();
        throw;
    }
}
void Store::prune(const Options &o, qint64 now) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM history WHERE created < ?");
    q.addBindValue(now - qint64(std::clamp(o.historyDays, 1, 365)) * 86400);
    checked(q);
    for (;;) {
        QSqlQuery totals("SELECT COUNT(*),COALESCE(SUM(bytes),0) FROM history", db_);
        if (!totals.next())
            break;
        if (totals.value(0).toInt() <= std::clamp(o.historyCount, 1, 500) &&
            totals.value(1).toLongLong() <= qint64(std::clamp(o.historyMiB, 1, 100)) * 1024 * 1024)
            break;
        exec("DELETE FROM history WHERE id=(SELECT id FROM history ORDER BY created,id LIMIT 1)");
    }
}
QVariantList Store::histories(bool full) const {
    QVariantList rows;
    QSqlQuery q(full ? "SELECT id,created,original,corrected,bytes FROM history ORDER BY created "
                       "DESC,id DESC"
                     : "SELECT id,created,substr(original,1,180),substr(corrected,1,180),bytes "
                       "FROM history ORDER BY created DESC,id DESC",
                db_);
    while (q.next())
        rows << QVariantMap{{"id", q.value(0)},
                            {"time", QDateTime::fromSecsSinceEpoch(q.value(1).toLongLong())
                                         .toString("MMM d, yyyy · HH:mm")},
                            {"original", q.value(2)},
                            {"corrected", q.value(3)},
                            {"bytes", q.value(4)}};
    return rows;
}
QVariantMap Store::historyDetail(qint64 id) const {
    QSqlQuery q(db_);
    q.prepare("SELECT original,corrected FROM history WHERE id=?");
    q.addBindValue(id);
    checked(q);
    if (!q.next())
        return {};
    return {{"id", id}, {"original", q.value(0)}, {"corrected", q.value(1)}};
}
void Store::deleteHistory(qint64 id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM history WHERE id=?");
    q.addBindValue(id);
    checked(q);
}
void Store::clearHistory() {
    exec("DELETE FROM history");
    exec("VACUUM");
}
bool Store::count(const QString &a, const QString &b, const Options &o) {
    if (!o.statistics || a == b || a.size() > 64 || b.size() > 64 || a.contains(' ') ||
        b.contains(' '))
        return false;
    QSqlQuery q(db_);
    q.prepare("INSERT INTO statistics VALUES(?,?,1) ON CONFLICT(original,corrected) DO UPDATE SET "
              "count=count+1");
    q.addBindValue(a);
    q.addBindValue(b);
    checked(q);
    // Bound unusual vocabulary growth independently of history retention.
    exec("DELETE FROM statistics WHERE rowid IN (SELECT rowid FROM statistics ORDER BY count "
         "DESC,rowid DESC LIMIT -1 OFFSET 10000)");
    return true;
}
QVariantList Store::statistics() const {
    QVariantList rows;
    QSqlQuery q("SELECT original,corrected,count FROM statistics ORDER BY count DESC LIMIT 200",
                db_);
    while (q.next())
        rows << QVariantMap{
            {"original", q.value(0)}, {"corrected", q.value(1)}, {"count", q.value(2)}};
    return rows;
}
void Store::clearStatistics() {
    exec("DELETE FROM statistics");
    exec("VACUUM");
}
void Store::addWord(const QString &w) {
    if (w.isEmpty() || w.size() > 64)
        return;
    QSqlQuery q(db_);
    q.prepare("INSERT OR IGNORE INTO words VALUES(?)");
    q.addBindValue(w.toLower());
    changeCorrectionConfiguration(q);
}
void Store::removeWord(const QString &w) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM words WHERE word=?");
    q.addBindValue(w.toLower());
    changeCorrectionConfiguration(q);
}
QSet<QString> Store::words() const {
    QSet<QString> result;
    QSqlQuery q("SELECT word FROM words", db_);
    while (q.next())
        result.insert(q.value(0).toString());
    return result;
}
void Store::replacement(const QString &a, const QString &b) {
    if (a.isEmpty() || a.size() > 64 || b.size() > 128)
        return;
    QSqlQuery q(db_);
    if (b.isEmpty()) {
        q.prepare("DELETE FROM replacements WHERE original=?");
        q.addBindValue(a.toLower());
    } else {
        q.prepare("INSERT INTO replacements VALUES(?,?) ON CONFLICT(original) DO UPDATE SET "
                  "corrected=excluded.corrected WHERE replacements.corrected<>excluded.corrected");
        q.addBindValue(a.toLower());
        q.addBindValue(b);
    }
    changeCorrectionConfiguration(q);
}
QHash<QString, QString> Store::replacements() const {
    QHash<QString, QString> r;
    QSqlQuery q("SELECT original,corrected FROM replacements", db_);
    while (q.next())
        r[q.value(0).toString()] = q.value(1).toString();
    return r;
}
bool Store::paused(qint64 now) const {
    return pauseActive(value("pauseUntil", 0).toLongLong(), now);
}
void Store::pause(int minutes) {
    setCorrectionOption("pauseUntil", minutes < 0    ? qint64(-1)
                                      : minutes == 0 ? qint64(0)
                                                     : QDateTime::currentSecsSinceEpoch() +
                                                           qint64(std::clamp(minutes, 1, 10080)) * 60);
}
} // namespace reword
