#include "store.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <stdexcept>
namespace reword {
static void checked(QSqlQuery &q){if(!q.exec())throw std::runtime_error("Local database operation failed.");}
void Store::exec(const QString &sql){QSqlQuery q(db_);q.prepare(sql);checked(q);}
Store::Store(const QString &directory) {
 if(QFileInfo(directory).isSymLink())throw std::runtime_error("Unsafe data directory.");
 if(!QDir().mkpath(directory))throw std::runtime_error("Cannot create private data directory.");
 QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
 auto path=directory+"/reword.sqlite";
 if(QFileInfo(path).isSymLink())throw std::runtime_error("Unsafe database path.");
 connection_=QUuid::createUuid().toString();db_=QSqlDatabase::addDatabase("QSQLITE",connection_);
 db_.setDatabaseName(path);if(!db_.open())throw std::runtime_error("Cannot open local database.");
 QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
 exec("PRAGMA busy_timeout=1000");exec("PRAGMA secure_delete=ON");
 exec("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
 exec("CREATE TABLE IF NOT EXISTS applications(id TEXT PRIMARY KEY,name TEXT NOT NULL,decision TEXT NOT NULL DEFAULT 'unknown',remind TEXT)");
 exec("CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY,created INTEGER NOT NULL,original TEXT NOT NULL,corrected TEXT NOT NULL,bytes INTEGER NOT NULL)");
 exec("CREATE TABLE IF NOT EXISTS statistics(original TEXT NOT NULL,corrected TEXT NOT NULL,count INTEGER NOT NULL,PRIMARY KEY(original,corrected))");
 exec("CREATE TABLE IF NOT EXISTS words(word TEXT PRIMARY KEY)");
 exec("CREATE TABLE IF NOT EXISTS replacements(original TEXT PRIMARY KEY,corrected TEXT NOT NULL)");
 prune(options());
}
Store::~Store(){db_.close();db_=QSqlDatabase();QSqlDatabase::removeDatabase(connection_);}
QVariant Store::value(const QString &key,const QVariant &fallback)const {
 QSqlQuery q(db_);q.prepare("SELECT value FROM settings WHERE key=?");q.addBindValue(key);checked(q);
 if(!q.next())return fallback;
 auto a=QJsonDocument::fromJson(q.value(0).toByteArray()).array();return a.isEmpty()?fallback:a.first().toVariant();
}
void Store::setValue(const QString &key,const QVariant &value) {
 QSqlQuery q(db_);q.prepare("INSERT INTO settings VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
 q.addBindValue(key);q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList({value})).toJson(QJsonDocument::Compact)));checked(q);
}
Options Store::options()const {
 Options o;
#define BOOL(k) o.k=value(#k,o.k).toBool()
 BOOL(english);BOOL(persian);BOOL(spelling);BOOL(abbreviations);BOOL(capitalization);BOOL(normalizePersian);BOOL(halfSpace);BOOL(periods);BOOL(commas);BOOL(spacing);BOOL(history);BOOL(statistics);BOOL(notifications);BOOL(browsers);
#undef BOOL
 o.sensitivity=std::clamp(value("sensitivity",0).toInt(),0,1);
 o.historyCount=std::clamp(value("historyCount",100).toInt(),1,500);
 o.historyMiB=std::clamp(value("historyMiB",20).toInt(),1,100);
 o.historyDays=std::clamp(value("historyDays",30).toInt(),1,365);
 o.maxCharacters=std::clamp(value("maxCharacters",50000).toInt(),100,100000);
 return o;
}
void Store::discover(const QString&id,const QString&name,bool restricted) {
 if(id.isEmpty() || id.size()>4096)return;
 QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO applications(id,name,decision) VALUES(?,?,?)");
 q.addBindValue(id);q.addBindValue(name.left(200));q.addBindValue(restricted?"deny":"unknown");checked(q);
}
void Store::decide(const QString&id,const QString&decision,QDate today) {
 if(decision!="allow" && decision!="deny" && decision!="later")return;
 QSqlQuery q(db_);q.prepare("UPDATE applications SET decision=?,remind=? WHERE id=?");
 q.addBindValue(decision);q.addBindValue(decision=="later"?today.addDays(1).toString(Qt::ISODate):QString(""));q.addBindValue(id);checked(q);
}
bool Store::allowed(const QString&id)const {
 QSqlQuery q(db_);q.prepare("SELECT decision FROM applications WHERE id=?");q.addBindValue(id);checked(q);return q.next() && q.value(0)=="allow";
}
bool Store::shouldAsk(const QString&id,QDate today)const {
 QSqlQuery q(db_);q.prepare("SELECT decision,remind FROM applications WHERE id=?");q.addBindValue(id);checked(q);
 if(!q.next())return false;
 return q.value(0)=="unknown" || (q.value(0)=="later" && QDate::fromString(q.value(1).toString(),Qt::ISODate)<=today);
}
QVariantList Store::applications()const {
 QVariantList rows;QSqlQuery q("SELECT id,name,decision,remind FROM applications ORDER BY lower(name)",db_);
 while(q.next())rows<<QVariantMap{{"id",q.value(0)},{"name",q.value(1)},{"decision",q.value(2)},{"remind",q.value(3)}};return rows;
}
void Store::history(const QString&a,const QString&b,const Options&o,qint64 now) {
 if(!o.history || a==b)return;
 qint64 bytes=a.toUtf8().size()+b.toUtf8().size();if(bytes>qint64(o.historyMiB)*1024*1024)return;
 if(!db_.transaction())throw std::runtime_error("Cannot begin history transaction.");
 try {
  QSqlQuery q(db_);q.prepare("INSERT INTO history(created,original,corrected,bytes) VALUES(?,?,?,?)");
  q.addBindValue(now);q.addBindValue(a);q.addBindValue(b);q.addBindValue(bytes);checked(q);prune(o,now);
  if(!db_.commit())throw std::runtime_error("Cannot commit history.");
 }catch(...){db_.rollback();throw;}
}
void Store::prune(const Options&o,qint64 now) {
 QSqlQuery q(db_);q.prepare("DELETE FROM history WHERE created < ?");q.addBindValue(now-qint64(std::clamp(o.historyDays,1,365))*86400);checked(q);
 for(;;){
  QSqlQuery totals("SELECT COUNT(*),COALESCE(SUM(bytes),0) FROM history",db_);if(!totals.next())break;
  if(totals.value(0).toInt()<=std::clamp(o.historyCount,1,500) && totals.value(1).toLongLong()<=qint64(std::clamp(o.historyMiB,1,100))*1024*1024)break;
  exec("DELETE FROM history WHERE id=(SELECT id FROM history ORDER BY created,id LIMIT 1)");
 }
}
QVariantList Store::histories()const {
 QVariantList rows;QSqlQuery q("SELECT id,created,original,corrected,bytes FROM history ORDER BY created DESC,id DESC",db_);
 while(q.next())rows<<QVariantMap{{"id",q.value(0)},{"time",QDateTime::fromSecsSinceEpoch(q.value(1).toLongLong()).toString("MMM d, yyyy · HH:mm")},{"original",q.value(2)},{"corrected",q.value(3)},{"bytes",q.value(4)}};return rows;
}
void Store::deleteHistory(qint64 id){QSqlQuery q(db_);q.prepare("DELETE FROM history WHERE id=?");q.addBindValue(id);checked(q);}
void Store::clearHistory(){exec("DELETE FROM history");exec("VACUUM");}
void Store::count(const QString&a,const QString&b,const Options&o) {
 if(!o.statistics || a==b || a.size()>64 || b.size()>64 || a.contains(' ') || b.contains(' '))return;
 QSqlQuery q(db_);q.prepare("INSERT INTO statistics VALUES(?,?,1) ON CONFLICT(original,corrected) DO UPDATE SET count=count+1");q.addBindValue(a);q.addBindValue(b);checked(q);
 // Bound unusual vocabulary growth independently of history retention.
 exec("DELETE FROM statistics WHERE rowid IN (SELECT rowid FROM statistics ORDER BY count DESC,rowid DESC LIMIT -1 OFFSET 10000)");
}
QVariantList Store::statistics()const {
 QVariantList rows;QSqlQuery q("SELECT original,corrected,count FROM statistics ORDER BY count DESC LIMIT 200",db_);
 while(q.next())rows<<QVariantMap{{"original",q.value(0)},{"corrected",q.value(1)},{"count",q.value(2)}};return rows;
}
void Store::clearStatistics(){exec("DELETE FROM statistics");exec("VACUUM");}
void Store::addWord(const QString&w){if(w.isEmpty()||w.size()>64)return;QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO words VALUES(?)");q.addBindValue(w.toLower());checked(q);}
void Store::removeWord(const QString&w){QSqlQuery q(db_);q.prepare("DELETE FROM words WHERE word=?");q.addBindValue(w.toLower());checked(q);}
QSet<QString> Store::words()const {QSet<QString> result;QSqlQuery q("SELECT word FROM words",db_);while(q.next())result.insert(q.value(0).toString());return result;}
void Store::replacement(const QString&a,const QString&b){if(a.isEmpty()||a.size()>64||b.size()>128)return;QSqlQuery q(db_);if(b.isEmpty()){q.prepare("DELETE FROM replacements WHERE original=?");q.addBindValue(a.toLower());}else{q.prepare("INSERT INTO replacements VALUES(?,?) ON CONFLICT(original) DO UPDATE SET corrected=excluded.corrected");q.addBindValue(a.toLower());q.addBindValue(b);}checked(q);}
QHash<QString,QString> Store::replacements()const{QHash<QString,QString> r;QSqlQuery q("SELECT original,corrected FROM replacements",db_);while(q.next())r[q.value(0).toString()]=q.value(1).toString();return r;}
bool Store::paused(qint64 now)const{auto until=value("pauseUntil",0).toLongLong();return until==-1 || until>now;}
void Store::pause(int minutes){setValue("pauseUntil",minutes<0?qint64(-1):minutes==0?qint64(0):QDateTime::currentSecsSinceEpoch()+qint64(std::clamp(minutes,1,10080))*60);}
}
