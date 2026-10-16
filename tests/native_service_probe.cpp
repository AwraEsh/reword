#include "app/service.h"
#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);app.setApplicationName("Reword integration probe");QTemporaryDir data;
 reword::Store store(data.path());auto fixturePath=QFileInfo(QCoreApplication::applicationDirPath()+"/reword_fixture").canonicalFilePath();
 store.discover(fixturePath,"Synthetic fixture");store.decide(fixturePath,"allow");store.setValue("statistics",true);
 auto observer=reword::createAdapter();reword::Service service(data.path());bool ready=false;QObject::connect(&service,&reword::Service::state,[&](QString,bool){ready=true;});service.start();
 QElapsedTimer startup;startup.start();while(!ready && startup.elapsed()<5000){app.processEvents();QThread::msleep(10);}
 if(!ready){service.stop();std::cerr<<"FAIL: service startup did not finish.\n";return 1;}
 QProcess fixture;fixture.start(fixturePath,{});if(!fixture.waitForStarted()){service.stop();return 2;}
 auto pump=[&](int milliseconds){QElapsedTimer t;t.start();while(t.elapsed()<milliseconds){app.processEvents();QThread::msleep(10);}};
 auto until=[&](auto predicate,int timeout=3000){QElapsedTimer t;t.start();do{pump(40);if(predicate())return true;}while(t.elapsed()<timeout);return false;};
 auto command=[&](QByteArray text){fixture.write(text+'\n');fixture.waitForBytesWritten();pump(160);};
 auto text=[&]{return observer->inspect(true).text;};
 auto finish=[&](const char*message,int code){std::cout<<message<<'\n';fixture.write("quit\n");fixture.waitForBytesWritten();fixture.waitForFinished(2000);service.stop();return code;};
 if(!until([&]{return observer->inspect(false).app==fixturePath;}))return finish("FAIL: fixture focus unavailable.",1);
 pump(300);command("text:");command("text:teh ");
 if(!until([&]{return text()=="the ";}))return finish("FAIL: first attempt was not corrected.",1);
 command("text:");command("text:teh ");pump(300);
 if(text()!="teh ")return finish("FAIL: second attempt was not preserved.",1);
 command("text:");command("text:teh ");
 if(!until([&]{return text()=="the ";}))return finish("FAIL: third attempt was not corrected.",1);
 service.action("undo");if(!until([&]{return text()=="teh ";}))return finish("FAIL: guarded live Undo failed.",1);
 store.pause(-1);command("text:");command("text:teh ");pump(300);
 if(text()!="teh ")return finish("FAIL: pause did not suppress correction.",1);
 store.pause(0);command("text:im here with teh book");command("select-all");service.action("fix");
 if(!until([&]{return text()=="I am here with the book";},12000))return finish("FAIL: selected correction failed.",1);
 if(store.histories().size()!=1)return finish("FAIL: selected history was not saved.",1);
 service.action("undo");if(!until([&]{return text()=="im here with teh book";}))return finish("FAIL: selected Undo failed.",1);
 command("password");auto password=observer->inspect(true);
 if(!password.password||!password.text.isEmpty())return finish("FAIL: password field protection failed.",1);
 service.action("fix");pump(100);if(store.histories().size()!=1)return finish("FAIL: protected operation was recorded.",1);
 return finish("PASS: live first/second/third, live Undo, pause, selected correction/history/Undo, password exclusion.",0);
}
