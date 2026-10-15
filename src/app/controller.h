#pragma once
#include "service.h"
#include <QThread>
#include <QSystemTrayIcon>
#include <QVariantMap>
namespace reword {
class Controller:public QObject {
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
 Q_PROPERTY(QString pendingName READ pendingName NOTIFY updated)
 Q_PROPERTY(bool busy READ busy NOTIFY updated)
 Q_PROPERTY(bool dark READ dark NOTIFY updated)
 Q_PROPERTY(bool paused READ paused NOTIFY updated)
 Store store_;QThread worker_;Service *service_=nullptr;QSystemTrayIcon *tray_=nullptr;
 QString platform_="Checking desktop integration…",notice_,pendingId_,pendingName_;
 QVariantList queue_;bool busy_=false,shortcuts_=false;
 QTimer refresh_;
 void refreshTray();void nextApplication();
public:
 explicit Controller(const QString&directory,QObject *parent=nullptr);
 ~Controller();
 QVariantMap settings()const;
 QVariantList apps()const{return store_.applications();}
 QVariantList history()const{return store_.histories(false);}
 QVariantList insights()const{return store_.statistics();}
 QStringList words()const{auto w=store_.words().values();w.sort();return w;}
 QVariantList replacements()const;
 QString status()const{return store_.paused()?"Paused":"Ready";}
 QString platform()const{return platform_;}
 QString notice()const{return notice_;}
 QString pendingName()const{return pendingName_;}
 bool busy()const{return busy_;}
 bool dark()const{return store_.value("dark",true).toBool();}
 bool paused()const{return store_.paused();}
 Q_INVOKABLE void set(QString key,QVariant value);
 Q_INVOKABLE void decide(QString id,QString decision);
 Q_INVOKABLE void consent(QString decision);
 Q_INVOKABLE void pause(int minutes);
 Q_INVOKABLE void fix(QString text);
 Q_INVOKABLE void cancel();
 Q_INVOKABLE void recordApplied(QString original,QString corrected);
 Q_INVOKABLE void copy(QString text);
 Q_INVOKABLE QVariantMap historyDetail(qint64 id)const{return store_.historyDetail(id);}
 Q_INVOKABLE void deleteHistory(qint64 id);
 Q_INVOKABLE void clearHistory();
 Q_INVOKABLE void clearStatistics();
 Q_INVOKABLE void addWord(QString word);
 Q_INVOKABLE void removeWord(QString word);
 Q_INVOKABLE void replacement(QString original,QString corrected);
 Q_INVOKABLE void clearNotice(){notice_.clear();emit updated();}
 Q_INVOKABLE void quit();
signals:
 void updated();
 void showWindow();
 void pauseDialog();
 void correctedText(QString original,QString corrected);
};
}
