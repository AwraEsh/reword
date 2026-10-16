#include "service.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QElapsedTimer>
namespace reword {
void Service::start(){
 try{store_=std::make_unique<Store>(directory_);engine_=std::make_unique<Engine>();adapter_=createAdapter();
 wayland_=qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland";
 timer_=new QTimer(this);connect(timer_,&QTimer::timeout,this,&Service::tick);timer_->start(80);
 }catch(...){emit message("Native integration could not start. Use the writing workspace.");}
}
void Service::stop(){cancel_.store(true);if(job_){job_->disconnect(this);job_->waitForFinished();delete job_;job_=nullptr;}jobLock_.reset();if(timer_)timer_->stop();previous_={};undoAfter_={};undoBefore_.clear();adapter_.reset();engine_.reset();store_.reset();}
Field Service::permitted(){
 auto f=adapter_->inspect(false);if(f.app.isEmpty())return {};
 store_->discover(f.app,QFileInfo(f.app).fileName(),restrictedApp(f.app));
 if(store_->shouldAsk(f.app) && (lastAsk_!=f.app || askDate_!=QDate::currentDate())){
  lastAsk_=f.app;askDate_=QDate::currentDate();emit application(f.app,QFileInfo(f.app).fileName());
 }
 Access access{store_->allowed(f.app),browserApp(f.app),store_->paused()};
 if(!safe(f,access,store_->options()))return {};
 auto read=adapter_->inspect(true);if(read.id!=f.id || read.app!=f.app || !safe(read,access,store_->options()))return {};
 return read;
}
void Service::tick(){
 if(!adapter_||!store_)return;
 try{
  auto signature=store_->value("fixShortcut","Ctrl+Alt+R").toString()+"|"+store_->value("undoShortcut","Ctrl+Alt+Z").toString()+"|"+store_->value("pauseShortcut","Ctrl+Alt+P").toString();
  if(signature!=shortcutSignature_){auto s=signature.split('|');bool ok=adapter_->shortcuts(s[0],s[1],s[2]);shortcutSignature_=signature;emit state(adapter_->description(),ok);}
  auto a=adapter_->takeAction();if(!a.isEmpty()){action(a);return;}
  if(!adapter_->changed())return;
  auto f=permitted();if(f.id.isEmpty()){previous_={};undoAfter_={};undoBefore_.clear();retry_.reset();retryStart_=-1;return;}
  if(f.id!=previous_.id){undoAfter_={};undoBefore_.clear();retry_.reset();retryStart_=-1;previous_=f;return;}
  if(f.text==previous_.text){if(f.caret!=previous_.caret)retry_.reset();previous_=f;return;}
  auto old=previous_;previous_=f;
  if(retryStart_>=0){
   static const QRegularExpression continued(R"([\s.,!?،؛؟]+[^\s.,!?،؛؟])");
   if(f.caret<retryStart_ || f.text.left(retryStart_)!=retryPrefix_ || continued.match(f.text.mid(retryStart_,f.caret-retryStart_)).hasMatch()){retry_.reset();retryStart_=-1;}
  }
  if(jobRunning_)return;
  if(wayland_ || f.caret<=0 || f.selectionStart>=0)return;
  if(f.text.size()<old.text.size())return; // Deleting a correction keeps its retry session.
  if(f.caret>f.text.size())return;
  auto prefix=f.text.left(f.caret);
  static const QRegularExpression ending(R"(([^\s]+?)([\s.,!?،؛؟]+)$)");
  auto match=ending.match(prefix);if(!match.hasMatch())return;
  auto token=match.captured(1);int start=match.capturedStart(1);
  if(start>0 && f.text.mid(0,start)!=old.text.mid(0,start))retry_.reset();
  auto o=store_->options();o.abbreviations=false;
  engine_->setPersonal(store_->words(),store_->replacements());
  auto correct=engine_->word(token,o,f.text.left(start).right(128));if(correct==token){if(retryStart_==start && token!=retryWord_){retry_.reset();retryStart_=-1;}return;}
  retryStart_=start;retryPrefix_=f.text.left(start);retryWord_=token;
  if(!retry_.shouldCorrect(f.id,start,token,QDateTime::currentMSecsSinceEpoch()))return;
  QString updated=f.text;updated.replace(start,token.size(),correct);
  if(adapter_->replace(f,updated,f.caret+correct.size()-token.size())){
   undoBefore_=f.text;undoCaret_=f.caret;undoAfter_=adapter_->inspect(true);previous_=undoAfter_;
   store_->count(token,correct,o);emit corrected(token,correct);
  }
 }catch(...){previous_={};retry_.reset();emit message("An integration operation failed safely. No further edit was attempted.");}
}
void Service::runText(QString input,bool native,Field field){
 if(jobRunning_){if(native)emit message("A correction is already running.");else emit result(input,input,"A correction is already running.",false);return;}
 if(!store_){emit result(input,input,"Correction engine is unavailable.",native);return;}
 auto operationLock=std::make_unique<QLockFile>(directory_+"/operation.lock");operationLock->setStaleLockTime(15000);
 if(!operationLock->tryLock(0)){emit result(input,input,"Another correction is already running.",native);return;}
 jobLock_=std::move(operationLock);jobRunning_=true;
 auto options=store_->options();auto words=store_->words();auto replacements=store_->replacements();
 auto revision=store_->value("configRevision",0).toLongLong();
 job_=new QFutureWatcher<Change>(this);
 connect(job_,&QFutureWatcher<Change>::finished,this,[this,input,native,field,revision]{
  auto operationLock=std::move(jobLock_);auto completed=job_;job_=nullptr;jobRunning_=false;completed->deleteLater();
  try{auto change=completed->result();
  if(cancel_.load() || revision!=store_->value("configRevision",0).toLongLong())change.error="Cancelled or settings changed. Nothing was replaced.";
  if(!change.error.isEmpty()){emit result(input,input,change.error,native);return;}
  if(!native){emit result(input,change.corrected,{},false);return;}
  if(!change.changed){emit message("No confident corrections found.");return;}
  try{
   auto current=permitted();if(!sameTarget(field,current)){emit message("The selection changed. Nothing was replaced.");return;}
   QString updated=field.text;updated.replace(field.selectionStart,field.selectionEnd-field.selectionStart,change.corrected);
   if(!adapter_->replace(field,updated,field.selectionStart+change.corrected.size())){emit message("The application did not confirm the replacement. Check its text before continuing.");return;}
   undoBefore_=field.text;undoCaret_=field.caret;undoAfter_=adapter_->inspect(true);previous_=undoAfter_;
   store_->history(input,change.corrected,store_->options());emit result(input,change.corrected,{},true);
  }catch(...){emit message("The operation stopped safely.");}
  }catch(...){emit result(input,input,"Local storage is unavailable. The operation stopped.",native);}
 });
 job_->setFuture(QtConcurrent::run([this,input,options,words,replacements]{
  try{QElapsedTimer elapsed;elapsed.start();Engine engine;engine.setPersonal(words,replacements);
   return engine.text(input,options,&cancel_,std::max(qint64(0),10000-elapsed.elapsed()));
  }catch(...){return Change{input,input,false,"Correction failed. The original is unchanged."};}
 }));
}
void Service::processText(QString input){try{runText(input,false);}catch(...){jobRunning_=false;jobLock_.reset();emit result(input,input,"Correction could not start. Check local storage access.",false);}}
void Service::fixSelection(){
 if(jobRunning_){emit message("A correction is already running.");return;}
 cancel_.store(false);auto field=permitted();
 if(field.id.isEmpty()||field.selectionStart<0||field.selectionEnd<=field.selectionStart){emit message("No supported, permitted text selection. Use the writing workspace instead.");return;}
 runText(field.text.mid(field.selectionStart,field.selectionEnd-field.selectionStart),true,field);
}
void Service::undo(){
 auto f=permitted();if(undoBefore_.isEmpty()||!sameTarget(undoAfter_,f)){emit message("Undo is no longer safe. Copy the original from History if this was a selected-text correction.");return;}
 if(adapter_->replace(f,undoBefore_,undoCaret_)){previous_=adapter_->inspect(true);undoBefore_.clear();undoAfter_={};emit message("Correction restored.");}
}
void Service::action(QString name){try{if(!adapter_||!store_)return;if(name=="fix")fixSelection();else if(name=="undo")undo();else if(name=="pause"){store_->pause(store_->paused()?0:-1);previous_={};retry_.reset();emit message(store_->paused()?"Reword paused.":"Reword resumed.");}}catch(...){if(!job_){jobRunning_=false;jobLock_.reset();}emit message("The operation stopped safely.");}}
}
