#include "controller.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QPainter>
#include <QKeySequence>
namespace reword {
Controller::Controller(const QString&directory,QObject*parent):QObject(parent),store_(directory){
 service_=new Service(directory);service_->moveToThread(&worker_);
 connect(&worker_,&QThread::started,service_,&Service::start);
 connect(&worker_,&QThread::finished,service_,&QObject::deleteLater);
 connect(service_,&Service::state,this,[this](QString text,bool keys){platform_=text;shortcuts_=keys;if(!keys)platform_+=" · global shortcuts unavailable";emit updated();});
 connect(service_,&Service::message,this,[this](QString text){notice_=text;emit updated();refreshTray();});
 connect(service_,&Service::application,this,[this](QString id,QString name){
  if(id==pendingId_)return;
  for(auto row:queue_)if(row.toMap()["id"]==id)return;
  queue_<<QVariantMap{{"id",id},{"name",name}};nextApplication();emit updated();
 });
 connect(service_,&Service::corrected,this,[this](QString a,QString b){
  if(read([&]{return store_.options();}).notifications && tray_)tray_->showMessage("Spelling corrected",a+" → "+b,QSystemTrayIcon::Information,3000);
  emit updated();
 });
 connect(service_,&Service::result,this,[this](QString original,QString corrected,QString error,bool native){
  busy_=false;
  if(!error.isEmpty())notice_=error;
  else if(native)notice_="Selected text corrected. The original is in History when history is enabled.";
  else emit correctedText(original,corrected);
  emit updated();
 });
 worker_.start();
 if(QSystemTrayIcon::isSystemTrayAvailable()){
  tray_=new QSystemTrayIcon(this);auto menu=new QMenu;
  menu->addAction("Open Reword",this,&Controller::showWindow);
  menu->addAction("Pause / Resume",this,[this]{pause(paused()?0:-1);});
  menu->addAction("Pause for…",this,&Controller::pauseDialog);
  menu->addSeparator();menu->addAction("Quit",this,&Controller::quit);
  tray_->setContextMenu(menu);connect(tray_,&QObject::destroyed,menu,&QObject::deleteLater);
  connect(tray_,&QSystemTrayIcon::activated,this,[this](auto reason){if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)emit showWindow();});
  connect(tray_,&QSystemTrayIcon::messageClicked,this,&Controller::showWindow);
  refreshTray();tray_->show();
 }else notice_="System tray is unavailable. Keep this window open to control Reword.";
 connect(&refresh_,&QTimer::timeout,this,[this]{guard([&]{
  for(auto row:store_.applications()){auto m=row.toMap();auto id=m["id"].toString();if(id.startsWith("fcitx:") && store_.shouldAsk(id) && id!=pendingId_){
   bool found=false;for(auto q:queue_)if(q.toMap()["id"]==id)found=true;
   if(!found)queue_<<QVariantMap{{"id",id},{"name",m["name"]}};
  }}nextApplication();refreshTray();emit updated();});});refresh_.start(10000);
}
Controller::~Controller(){service_->cancel();QMetaObject::invokeMethod(service_,&Service::stop,Qt::BlockingQueuedConnection);worker_.quit();worker_.wait();}
QVariantMap Controller::settings()const {
 QVariantMap m;auto o=read([&]{return store_.options();});
#define SET(k) m[#k]=o.k
 SET(english);SET(persian);SET(spelling);SET(abbreviations);SET(capitalization);SET(normalizePersian);SET(halfSpace);SET(periods);SET(commas);SET(spacing);SET(paragraphPeriods);SET(introductoryCommas);SET(questionMarks);SET(exclamationMarks);SET(semicolons);SET(colons);SET(history);SET(statistics);SET(notifications);SET(browsers);SET(sensitivity);SET(historyCount);SET(historyMiB);SET(historyDays);SET(maxCharacters);
#undef SET
 m["fixShortcut"]=read([&]{return store_.value("fixShortcut","Ctrl+Alt+R");});m["undoShortcut"]=read([&]{return store_.value("undoShortcut","Ctrl+Alt+Z");});m["pauseShortcut"]=read([&]{return store_.value("pauseShortcut","Ctrl+Alt+P");});
 m["shortcutsAvailable"]=shortcuts_;return m;
}
QVariantList Controller::replacements()const{QVariantList list;auto map=read([&]{return store_.replacements();});for(auto i=map.cbegin();i!=map.cend();++i)list<<QVariantMap{{"original",i.key()},{"corrected",i.value()}};return list;}
int Controller::characterCount(const QString&text)const{
 if(text.size()>200000)return 200001;
 int count=0;for(qsizetype i=0;i<text.size();++i){if(text[i].isHighSurrogate()&&i+1<text.size()&&text[i+1].isLowSurrogate())++i;++count;}return count;
}
void Controller::set(QString key,QVariant value){guard([&]{
 if(!settings().contains(key)&&key!="dark")return;
 if(key.endsWith("Shortcut")){
  QKeySequence seq(value.toString());if(seq.count()!=1 || seq[0].key()<Qt::Key_A || seq[0].key()>Qt::Key_Z || seq[0].keyboardModifiers()==Qt::NoModifier){notice_="Use one letter with Ctrl, Alt, Shift or Meta.";emit updated();return;}
  value=seq.toString(QKeySequence::PortableText);
  for(auto other:{"fixShortcut","undoShortcut","pauseShortcut"})if(key!=other && QKeySequence(settings()[other].toString())==seq){notice_="This shortcut is already assigned in Reword.";emit updated();return;}
 }
 store_.setValue(key,value);store_.setValue("configRevision",store_.value("configRevision",0).toLongLong()+1);service_->cancel();store_.prune(store_.options());emit updated();
});}
void Controller::decide(QString id,QString decision){guard([&]{store_.decide(id,decision);emit updated();});}
void Controller::nextApplication(){guard([&]{
 if(!pendingId_.isEmpty())return;
 while(!queue_.isEmpty()){
  auto row=queue_.takeFirst().toMap();if(!store_.shouldAsk(row["id"].toString()))continue;
  pendingId_=row["id"].toString();pendingName_=row["name"].toString();
  if(tray_)tray_->showMessage("Allow Reword in "+pendingName_+"?","Open Reword to choose Yes, Not now or No.",QSystemTrayIcon::Information,6000);
  emit showWindow();break;
 }
 emit updated();
});}
void Controller::consent(QString decision){guard([&]{if(!pendingId_.isEmpty() && !decision.isEmpty())store_.decide(pendingId_,decision);pendingId_.clear();pendingName_.clear();nextApplication();emit updated();});}
void Controller::pause(int minutes){guard([&]{store_.pause(minutes);refreshTray();emit updated();});}
void Controller::fix(QString text){guard([&]{
 if(busy_){notice_="A correction is already running.";emit updated();return;}
 if(text.size()>2*store_.options().maxCharacters || text.toUcs4().size()>store_.options().maxCharacters){notice_="Text exceeds the operation limit. Nothing was changed.";emit updated();return;}
 service_->prepare();busy_=true;notice_.clear();emit updated();QMetaObject::invokeMethod(service_,[this,text]{service_->processText(text);},Qt::QueuedConnection);
});}
void Controller::recordApplied(QString original,QString corrected){guard([&]{store_.history(original,corrected,store_.options());emit updated();});}
void Controller::cancel(){service_->cancel();notice_="Cancelling…";emit updated();}
void Controller::copy(QString text){guard([&]{QApplication::clipboard()->setText(text);notice_="Copied.";emit updated();});}
void Controller::deleteHistory(qint64 id){guard([&]{store_.deleteHistory(id);emit updated();});}
void Controller::clearHistory(){guard([&]{store_.clearHistory();emit updated();});}
void Controller::clearStatistics(){guard([&]{store_.clearStatistics();emit updated();});}
void Controller::addWord(QString word){guard([&]{store_.addWord(word.trimmed());emit updated();});}
void Controller::removeWord(QString word){guard([&]{store_.removeWord(word);emit updated();});}
void Controller::replacement(QString a,QString b){guard([&]{store_.replacement(a.trimmed(),b);emit updated();});}
void Controller::refreshTray(){
 if(!tray_)return;QPixmap p(32,32);p.fill(Qt::transparent);QPainter paint(&p);paint.setRenderHint(QPainter::Antialiasing);
 paint.setPen(Qt::NoPen);paint.setBrush(paused()?QColor("#8d949e"):QColor("#72c6b0"));paint.drawRoundedRect(2,2,28,28,8,8);
 paint.setPen(QColor("#111815"));QFont font;font.setPixelSize(22);font.setBold(true);paint.setFont(font);paint.drawText(p.rect(),Qt::AlignCenter,"r");
 tray_->setIcon(QIcon(p));tray_->setToolTip("Reword · "+status());
}
void Controller::quit(){QApplication::quit();}
}
