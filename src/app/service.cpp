#include "service.h"
#include <QFileInfo>
#include <QRegularExpression>
namespace reword {
void Service::start(){
 try{store_=std::make_unique<Store>(directory_);engine_=std::make_unique<Engine>();adapter_=createAdapter();
 wayland_=qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland";
 timer_=new QTimer(this);connect(timer_,&QTimer::timeout,this,&Service::tick);timer_->start(80);
 }catch(...){emit message("Native integration could not start. Use the writing workspace.");}
}
void Service::stop(){if(timer_)timer_->stop();previous_={};undoAfter_={};undoBefore_.clear();adapter_.reset();engine_.reset();store_.reset();}
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
  auto f=permitted();if(f.id.isEmpty()){previous_={};retry_.reset();return;}
  if(f.id!=previous_.id){retry_.reset();previous_=f;return;}
  if(f.text==previous_.text){if(f.caret!=previous_.caret)retry_.reset();previous_=f;return;}
  auto old=previous_;previous_=f;
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
  auto correct=engine_->word(token,o,f.text.left(start).right(128));if(correct==token)return;
  if(!retry_.shouldCorrect(f.id,start,token,QDateTime::currentMSecsSinceEpoch()))return;
  QString updated=f.text;updated.replace(start,token.size(),correct);
  if(adapter_->replace(f,updated,f.caret+correct.size()-token.size())){
   undoBefore_=f.text;undoCaret_=f.caret;undoAfter_=adapter_->inspect(true);previous_=undoAfter_;
   store_->count(token,correct,o);emit corrected(token,correct);
  }
 }catch(...){previous_={};retry_.reset();emit message("An integration operation failed safely. No further edit was attempted.");}
}
void Service::processText(QString input){
 cancel_.store(false);
 try{if(!engine_||!store_){emit result(input,input,"Correction engine is unavailable.",false);return;}
  engine_->setPersonal(store_->words(),store_->replacements());auto o=store_->options();auto change=engine_->text(input,o,&cancel_);
  emit result(input,change.corrected,change.error,false);
 }catch(...){emit result(input,input,"Correction failed. The original text is unchanged.",false);}
}
void Service::fixSelection(){
 cancel_.store(false);
 auto f=permitted();if(f.id.isEmpty()||f.selectionStart<0||f.selectionEnd<=f.selectionStart){emit message("No supported, permitted text selection. Use the writing workspace instead.");return;}
 auto input=f.text.mid(f.selectionStart,f.selectionEnd-f.selectionStart);auto o=store_->options();
 engine_->setPersonal(store_->words(),store_->replacements());auto change=engine_->text(input,o,&cancel_);
 if(!change.error.isEmpty()){emit result(input,input,change.error,true);return;}
 if(!change.changed){emit message("No confident corrections found.");return;}
 auto current=permitted();if(!sameTarget(f,current)){emit message("The selection changed. Nothing was replaced.");return;}
 QString updated=f.text;updated.replace(f.selectionStart,f.selectionEnd-f.selectionStart,change.corrected);
 if(!adapter_->replace(f,updated,f.selectionStart+change.corrected.size())){emit message("The application did not confirm the replacement. Check its text before continuing.");return;}
 undoBefore_=f.text;undoCaret_=f.caret;undoAfter_=adapter_->inspect(true);previous_=undoAfter_;
 store_->history(input,change.corrected,o);emit result(input,change.corrected,{},true);
}
void Service::undo(){
 auto f=permitted();if(undoBefore_.isEmpty()||!sameTarget(undoAfter_,f)){emit message("Undo is no longer safe. Copy the original from History if this was a selected-text correction.");return;}
 if(adapter_->replace(f,undoBefore_,undoCaret_)){previous_=adapter_->inspect(true);undoBefore_.clear();undoAfter_={};emit message("Correction restored.");}
}
void Service::action(QString name){try{if(!adapter_||!store_)return;if(name=="fix")fixSelection();else if(name=="undo")undo();else if(name=="pause"){store_->pause(store_->paused()?0:-1);previous_={};retry_.reset();emit message(store_->paused()?"Reword paused.":"Reword resumed.");}}catch(...){emit message("The operation stopped safely.");}}
}
