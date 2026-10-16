#include "wire.h"
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QElapsedTimer>
namespace reword {
class LinuxProxy final:public Adapter {
 QProcess child_;QByteArray pending_;bool healthy_=false;
 QJsonObject request(QJsonObject input){
  if(!healthy_)return {};
  auto bytes=QJsonDocument(input).toJson(QJsonDocument::Compact)+'\n';
  if(bytes.size()>1024*1024){healthy_=false;child_.kill();return {};}
  if(child_.write(bytes)!=bytes.size()){healthy_=false;return {};}
  QElapsedTimer time;time.start();
  while(time.elapsed()<2000){
   pending_+=child_.readAllStandardOutput();
   if(pending_.size()>1024*1024)break;
   auto end=pending_.indexOf('\n');if(end>=0){auto frame=pending_.left(end);pending_.remove(0,end+1);QJsonParseError error;auto doc=QJsonDocument::fromJson(frame,&error);if(error.error==QJsonParseError::NoError&&doc.isObject())return doc.object();break;}
   if(child_.state()!=QProcess::Running)break;
   child_.waitForReadyRead(50);
  }
  healthy_=false;pending_.clear();child_.kill();return {};
 }
public:
 LinuxProxy(){
  child_.setProgram(QCoreApplication::applicationDirPath()+"/reword-native-helper");
  child_.setStandardErrorFile(QProcess::nullDevice()); // Native libraries must not log user text.
  child_.start();healthy_=child_.waitForStarted(2000);
 }
 ~LinuxProxy()override{child_.closeWriteChannel();if(!child_.waitForFinished(500)){child_.terminate();if(!child_.waitForFinished(500)){child_.kill();child_.waitForFinished(500);}}}
 Field inspect(bool text)override{return decodeField(request({{"operation","inspect"},{"read",text}})["field"].toObject());}
 bool changed()override{return request({{"operation","changed"}})["changed"].toBool();}
 bool replace(const Field &expected,const QString &text,int caret)override{return request({{"operation","replace"},{"expected",encodeField(expected)},{"text",text},{"caret",caret}})["ok"].toBool();}
 QString description()const override{return healthy_?const_cast<LinuxProxy*>(this)->request({{"operation","description"}})["description"].toString():"Native helper unavailable · writing workspace only";}
 bool shortcuts(const QString&fix,const QString&undo,const QString&pause)override{return request({{"operation","shortcuts"},{"fix",fix},{"undo",undo},{"pause",pause}})["ok"].toBool();}
 QString takeAction()override{return request({{"operation","action"}})["action"].toString();}
};
std::unique_ptr<Adapter> createAdapter(){return std::make_unique<LinuxProxy>();}
}
