#include "core/core.h"
#include "core/store.h"
#include "platform/adapter.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
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

namespace {
class RewordAddon final:public fcitx::AddonInstance {
 int argc_=1;char name_[7]="reword";char *argv_[2]={name_,nullptr};
 std::unique_ptr<QCoreApplication> qt_;
 std::unique_ptr<reword::Store> store_;std::unique_ptr<reword::Engine> engine_;
 std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> key_,focus_;
 reword::RetryCycle retry_;QString context_,prefix_;qint64 heartbeat_=0;
 bool unlocked(){
  for(auto spec:{QStringList{"org.gnome.ScreenSaver","/org/gnome/ScreenSaver","org.gnome.ScreenSaver"},QStringList{"org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver"}}){
   QDBusInterface d(spec[0],spec[1],spec[2],QDBusConnection::sessionBus());d.setTimeout(80);QDBusReply<bool> r=d.call("GetActive");if(r.isValid())return !r.value();
  }return false;
 }
 bool hotkey(const fcitx::Key &key,QString setting,QString fallback){
  auto s=store_->value(setting,fallback).toString().replace("Ctrl+","Control+");if(!s.isEmpty())s[s.size()-1]=s.back().toLower();
  return key.check(fcitx::Key(s.toStdString()));
 }
 void handle(fcitx::Event &event){
  auto &e=static_cast<fcitx::KeyEvent&>(event);auto *ic=e.inputContext();if(!ic||!ic->hasFocus()||e.isRelease())return;
  try{
   auto app=QString::fromStdString(ic->program());if(app.isEmpty())return;
   // Fcitx program names are a separate consent identity; never infer consent from a similar executable.
   QString id="fcitx:"+app;store_->discover(id,app,reword::restrictedApp(app));
   auto now=QDateTime::currentSecsSinceEpoch();if(now!=heartbeat_){store_->setValue("fcitxHeartbeat",now);heartbeat_=now;}
   auto flags=ic->capabilityFlags();
   if(flags.test(fcitx::CapabilityFlag::Password)||flags.test(fcitx::CapabilityFlag::Sensitive)||!flags.test(fcitx::CapabilityFlag::SurroundingText)){retry_.reset();return;}
   auto o=store_->options();
   if(hotkey(e.key(),"pauseShortcut","Ctrl+Alt+P")){store_->pause(store_->paused()?0:-1);e.filterAndAccept();return;}
   if(store_->paused()||!store_->allowed(id)||(reword::browserApp(app)&&!o.browsers)){retry_.reset();return;}
   bool selection=hotkey(e.key(),"fixShortcut","Ctrl+Alt+R");
   auto code=fcitx::Key::keySymToUnicode(e.key().sym());
   if(!selection && code!=' ' && code!='.' && code!=',' && code!='!' && code!='?' && code!=0x060c && code!=0x061f)return;
   if(!unlocked())return;
   const auto &st=ic->surroundingText();if(!st.isValid()||st.text().size()>400000)return;
   auto text=QString::fromStdString(st.text());auto points=text.toUcs4();
   if(st.cursor()>unsigned(points.size())||st.anchor()>unsigned(points.size()))return;
   auto current=QString::number(reinterpret_cast<quintptr>(ic),16);
   if(current!=context_){retry_.reset();context_=current;prefix_.clear();}
   engine_->setPersonal(store_->words(),store_->replacements());
   if(selection){
    if(st.cursor()==st.anchor())return;
    auto original=QString::fromStdString(st.selectedText());auto change=engine_->text(original,o);
    if(!change.error.isEmpty()||!change.changed)return;
    // A commit into an existing selection is one input-method operation.
    if(!ic->hasFocus()||!st.isValid()||QString::fromStdString(st.text())!=text)return;
    ic->commitString(change.corrected.toStdString());store_->history(original,change.corrected,o);e.filterAndAccept();return;
   }
   if(st.cursor()!=st.anchor() || !ic->inputPanel().clientPreedit().empty())return;
   auto before=QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()),st.cursor());
   static const QRegularExpression end(R"(([^\s.,!?،؛؟]+)$)");auto m=end.match(before);if(!m.hasMatch()){retry_.reset();return;}
   auto token=m.captured();auto prefix=before.left(m.capturedStart());
   if(prefix!=prefix_){retry_.reset();prefix_=prefix;}
   o.abbreviations=false;auto correction=engine_->word(token,o,prefix.right(128));if(correction==token)return;
   if(!retry_.shouldCorrect(current,m.capturedStart(),token,QDateTime::currentMSecsSinceEpoch()))return;
   if(!ic->hasFocus()||!st.isValid()||QString::fromStdString(st.text())!=text)return;
   ic->deleteSurroundingText(-int(token.toUcs4().size()),token.toUcs4().size());
   ic->commitString(correction.toStdString());store_->count(token,correction,o);
   // Let the original delimiter proceed to the input method/application.
  }catch(...){retry_.reset();}
 }
public:
 explicit RewordAddon(fcitx::Instance *instance){
  if(!QCoreApplication::instance())qt_=std::make_unique<QCoreApplication>(argc_,argv_);
  auto dir=qEnvironmentVariable("REWORD_DATA_DIR");if(dir.isEmpty())dir=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/reword";
  store_=std::make_unique<reword::Store>(dir);engine_=std::make_unique<reword::Engine>();
  key_=instance->watchEvent(fcitx::EventType::InputContextKeyEvent,fcitx::EventWatcherPhase::PreInputMethod,[this](fcitx::Event&e){handle(e);});
  focus_=instance->watchEvent(fcitx::EventType::InputContextFocusOut,fcitx::EventWatcherPhase::Default,[this](fcitx::Event&){retry_.reset();context_.clear();prefix_.clear();});
 }
};
class Factory:public fcitx::AddonFactory {public:fcitx::AddonInstance*create(fcitx::AddonManager*m)override{try{return new RewordAddon(m->instance());}catch(...){return nullptr;}}};
}
FCITX_ADDON_FACTORY(Factory)
