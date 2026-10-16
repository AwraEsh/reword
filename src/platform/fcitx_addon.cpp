#include "core/core.h"
#include "core/store.h"
#include "platform/adapter.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QLockFile>
#include <future>
#undef emit
#undef signals
#undef slots
#undef foreach
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/event.h>
#include <fcitx-utils/event.h>

namespace {
class RewordAddon final:public fcitx::AddonInstance {
 int argc_=1;char name_[7]="reword";char *argv_[2]={name_,nullptr};
 std::unique_ptr<QCoreApplication> qt_;
 std::unique_ptr<reword::Store> store_;std::unique_ptr<reword::Engine> engine_;
 std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> key_,focus_;
 std::unique_ptr<fcitx::EventSourceTime> timer_;
 reword::RetryCycle retry_;QString context_,prefix_;qint64 heartbeat_=0;
 QString directory_;std::unique_ptr<QLockFile> operationLock_;
 std::future<reword::Change> future_;std::atomic_bool cancel_{false};
 struct Edit {
  fcitx::TrackableObjectReference<fcitx::InputContext> context;
  QString app,before,after,original,corrected;
  unsigned cursor=0,anchorBefore=0,cursorBefore=0;int start=0;qint64 revision=0,expires=0;
  bool passage=false,valid=false,confirmed=false;
 } pending_,job_;
 bool unlocked(){
  for(auto spec:{QStringList{"org.gnome.ScreenSaver","/org/gnome/ScreenSaver","org.gnome.ScreenSaver"},QStringList{"org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver"}}){
   QDBusInterface d(spec[0],spec[1],spec[2],QDBusConnection::sessionBus());d.setTimeout(80);QDBusReply<bool> r=d.call("GetActive");if(r.isValid())return !r.value();
  }return false;
 }
 bool permitted(fcitx::InputContext *ic,const QString&id){
  if(!ic||!ic->hasFocus())return false;auto flags=ic->capabilityFlags();
  if(flags.test(fcitx::CapabilityFlag::Password)||flags.test(fcitx::CapabilityFlag::Sensitive)||!flags.test(fcitx::CapabilityFlag::SurroundingText))return false;
  return !store_->paused()&&store_->allowed(id)&&(!reword::browserApp(id.mid(6))||store_->options().browsers);
 }
 bool hotkey(const fcitx::Key &key,QString setting,QString fallback){
  auto s=store_->value(setting,fallback).toString().replace("Ctrl+","Control+");if(!s.isEmpty())s[s.size()-1]=s.back().toLower();return key.check(fcitx::Key(s.toStdString()));
 }
 void observe(fcitx::InputContext *ic){
  if(!pending_.valid||pending_.context.get()!=ic)return;
  const auto &st=ic->surroundingText();
  if(QDateTime::currentMSecsSinceEpoch()>pending_.expires){pending_={};return;}
  if(st.isValid()&&QString::fromStdString(st.text())==pending_.after&&st.cursor()==pending_.cursor&&st.anchor()==pending_.cursor){
   if(!pending_.confirmed){
    auto o=store_->options();if(pending_.passage)store_->history(pending_.original,pending_.corrected,o);else store_->count(pending_.original,pending_.corrected,o);
    pending_.confirmed=true;
    if(o.notifications&&!pending_.passage){
     QDBusInterface n("org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications",QDBusConnection::sessionBus());
     n.asyncCall("Notify","Reword",uint(0),"", "Spelling corrected",pending_.original+" → "+pending_.corrected,QStringList{},QVariantMap{},3000);
    }
   }
  }
 }
 void completed(){
  if(!future_.valid()||future_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
  auto operationLock=std::move(operationLock_);auto change=future_.get();auto job=job_;job_={};auto ic=job.context.get();
  if(cancel_.load()||!change.changed||!change.error.isEmpty()||!permitted(ic,job.app)||!unlocked()||job.revision!=store_->value("configRevision",0).toLongLong())return;
  const auto &st=ic->surroundingText();if(!st.isValid()||QString::fromStdString(st.text())!=job.before||QString::fromStdString(st.selectedText())!=job.original||st.anchor()!=job.anchorBefore||st.cursor()!=job.cursorBefore)return;
  auto points=job.before.toUcs4();unsigned start=std::min(st.anchor(),st.cursor()),end=std::max(st.anchor(),st.cursor());
  QString before=QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()),start);
  QString after=QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()+end),points.size()-end);
  job.after=before+change.corrected+after;job.corrected=change.corrected;job.cursor=start+change.corrected.toUcs4().size();job.start=before.size();job.valid=true;job.expires=QDateTime::currentMSecsSinceEpoch()+30000;
  pending_=job;ic->commitString(change.corrected.toStdString());
 }
 void handle(fcitx::Event &event){
  // X11 is owned by the accessibility service. Never run two correction cycles.
  if(qEnvironmentVariable("XDG_SESSION_TYPE")!="wayland")return;
  auto &e=static_cast<fcitx::KeyEvent&>(event);auto *ic=e.inputContext();if(!ic||!ic->hasFocus()||e.isRelease())return;
  try{
   auto app=QString::fromStdString(ic->program());if(app.isEmpty())return;QString id="fcitx:"+app;
   store_->discover(id,app,reword::restrictedApp(app));auto now=QDateTime::currentSecsSinceEpoch();if(now!=heartbeat_){store_->setValue("fcitxHeartbeat",now);heartbeat_=now;}
   if(hotkey(e.key(),"pauseShortcut","Ctrl+Alt+P")){cancel_.store(true);store_->pause(store_->paused()?0:-1);e.filterAndAccept();return;}
   if(!permitted(ic,id)){retry_.reset();pending_={};cancel_.store(true);return;}
   bool selection=hotkey(e.key(),"fixShortcut","Ctrl+Alt+R"),undo=hotkey(e.key(),"undoShortcut","Ctrl+Alt+Z");
   auto code=fcitx::Key::keySymToUnicode(e.key().sym());
   const auto &st=ic->surroundingText();if(!st.isValid()||st.text().size()>400000)return;
   auto text=QString::fromStdString(st.text());auto points=text.toUcs4();if(st.cursor()>unsigned(points.size())||st.anchor()>unsigned(points.size()))return;
   observe(ic);
   if(undo){
    if(!unlocked()||!pending_.valid||!pending_.confirmed||pending_.context.get()!=ic||text!=pending_.after||st.cursor()!=pending_.cursor||st.anchor()!=st.cursor())return;
    auto original=pending_.original;int length=pending_.corrected.toUcs4().size();
    // Live edits include the delimiter, so restore the word and delimiter together.
    if(!pending_.passage){auto suffix=pending_.after.mid(pending_.start+pending_.corrected.size(),1);original+=suffix;++length;}
    ic->deleteSurroundingText(-length,length);ic->commitString(original.toStdString());pending_={};e.filterAndAccept();return;
   }
   if(selection){
    if(future_.valid()||st.cursor()==st.anchor()||!unlocked())return;
    auto original=QString::fromStdString(st.selectedText());auto options=store_->options();
    if(original.size()>2*options.maxCharacters||original.toUcs4().size()>options.maxCharacters)return;
    auto lock=std::make_unique<QLockFile>(directory_+"/operation.lock");lock->setStaleLockTime(15000);if(!lock->tryLock(0))return;operationLock_=std::move(lock);
    cancel_.store(false);job_={};job_.context=ic->watch();job_.app=id;job_.before=text;job_.original=original;job_.anchorBefore=st.anchor();job_.cursorBefore=st.cursor();job_.passage=true;job_.revision=store_->value("configRevision",0).toLongLong();
    auto words=store_->words();auto replacements=store_->replacements();
    future_=std::async(std::launch::async,[this,original,options,words,replacements]{try{QElapsedTimer elapsed;elapsed.start();reword::Engine engine;engine.setPersonal(words,replacements);return engine.text(original,options,&cancel_,std::max(qint64(0),10000-elapsed.elapsed()));}catch(...){return reword::Change{original,original,false,"Correction failed."};}});
    e.filterAndAccept();return;
   }
   if(future_.valid()|| (code!=' '&&code!='.'&&code!=','&&code!='!'&&code!='?'&&code!=0x060c&&code!=0x061f))return;
   if(!unlocked()||st.cursor()!=st.anchor()||!ic->inputPanel().clientPreedit().empty())return;
   auto current=QString::number(reinterpret_cast<quintptr>(ic),16);if(current!=context_){retry_.reset();context_=current;prefix_.clear();}
   auto before=QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()),st.cursor());
   static const QRegularExpression end(R"(([^\s.,!?،؛؟]+)$)");auto m=end.match(before);if(!m.hasMatch()){retry_.reset();return;}
   auto token=m.captured(),prefix=before.left(m.capturedStart());if(prefix!=prefix_){retry_.reset();prefix_=prefix;}
   auto options=store_->options();options.abbreviations=false;engine_->setPersonal(store_->words(),store_->replacements());auto corrected=engine_->word(token,options,prefix.right(128));if(corrected==token)return;
   if(!retry_.shouldCorrect(current,m.capturedStart(),token,QDateTime::currentMSecsSinceEpoch()))return;
   auto suffix=QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()+st.cursor()),points.size()-st.cursor());
   char32_t delimiter=code;auto mark=QString::fromUcs4(&delimiter,1);
   pending_={};pending_.context=ic->watch();pending_.app=id;pending_.before=text;pending_.after=prefix+corrected+mark+suffix;pending_.original=token;pending_.corrected=corrected;pending_.start=prefix.size();pending_.cursor=prefix.toUcs4().size()+corrected.toUcs4().size()+1;pending_.valid=true;pending_.expires=QDateTime::currentMSecsSinceEpoch()+30000;
   ic->deleteSurroundingText(-int(token.toUcs4().size()),token.toUcs4().size());ic->commitString(corrected.toStdString());
  }catch(...){retry_.reset();pending_={};cancel_.store(true);}
 }
public:
 explicit RewordAddon(fcitx::Instance *instance){
  if(!QCoreApplication::instance())qt_=std::make_unique<QCoreApplication>(argc_,argv_);
  auto dir=qEnvironmentVariable("REWORD_DATA_DIR");if(dir.isEmpty())dir=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/reword";
  directory_=dir;store_=std::make_unique<reword::Store>(dir);engine_=std::make_unique<reword::Engine>();
  key_=instance->watchEvent(fcitx::EventType::InputContextKeyEvent,fcitx::EventWatcherPhase::PreInputMethod,[this](fcitx::Event&e){handle(e);});
  focus_=instance->watchEvent(fcitx::EventType::InputContextFocusOut,fcitx::EventWatcherPhase::Default,[this](fcitx::Event&){retry_.reset();context_.clear();prefix_.clear();pending_={};cancel_.store(true);});
  timer_=instance->eventLoop().addTimeEvent(CLOCK_MONOTONIC,fcitx::now(CLOCK_MONOTONIC)+40000,0,[this](fcitx::EventSourceTime *timer,uint64_t){try{completed();if(pending_.valid){auto ic=pending_.context.get();if(permitted(ic,pending_.app)&&unlocked())observe(ic);else pending_={};}}catch(...){cancel_.store(true);pending_={};}timer->setNextInterval(40000);timer->setEnabled(true);return true;});
 }
 ~RewordAddon()override{cancel_.store(true);if(future_.valid())future_.wait();}
};
class Factory:public fcitx::AddonFactory {public:fcitx::AddonInstance*create(fcitx::AddonManager*m)override{try{return new RewordAddon(m->instance());}catch(...){return nullptr;}}};
}
FCITX_ADDON_FACTORY(Factory)
