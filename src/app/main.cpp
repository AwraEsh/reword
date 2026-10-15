#include "controller.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QLockFile>
#include <QDir>
#include <QTimer>
#include <QQuickWindow>
#include <iostream>
#ifndef _WIN32
#include <sys/stat.h>
#endif
int main(int argc,char **argv){
#ifndef _WIN32
 umask(0077);
#endif
 QApplication app(argc,argv);app.setOrganizationName("Reword");app.setApplicationName("Reword");app.setApplicationVersion("0.1.0");
 app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
 QString directory=qEnvironmentVariable("REWORD_DATA_DIR");if(directory.isEmpty())directory=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/reword";
 QDir().mkpath(directory);QLockFile lock(directory+"/instance.lock");lock.setStaleLockTime(0);
 if(!lock.tryLock(100)){std::cerr<<"Reword is already running. Open it from the tray.\n";return 1;}
 try{
  reword::Controller controller(directory);QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("Backend",&controller);
  QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(2);},Qt::QueuedConnection);
  engine.load(QUrl("qrc:/Reword/qml/Main.qml"));
  if(!qEnvironmentVariable("REWORD_CAPTURE").isEmpty())QTimer::singleShot(1500,&app,[&]{
   if(!engine.rootObjects().isEmpty()){
    auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(window){window->setProperty("page",qEnvironmentVariableIntValue("REWORD_CAPTURE_PAGE"));QTimer::singleShot(500,window,[window]{window->grabWindow().save(qEnvironmentVariable("REWORD_CAPTURE"));});}
   }
  });
  if(app.arguments().contains("--smoke-test"))QTimer::singleShot(3000,&app,&QCoreApplication::quit);
  return app.exec();
 }catch(...){std::cerr<<"Reword could not open its local data. Check directory access.\n";return 3;}
}
