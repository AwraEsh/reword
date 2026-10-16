#include <QtTest>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "app/controller.h"
class UiTest:public QObject {
 Q_OBJECT
private slots:
 void passageLockRejectsConcurrentWork(){
  QTemporaryDir data;reword::Controller first(data.path()),second(data.path());
  QSignalSpy one(&first,&reword::Controller::correctedText),two(&second,&reword::Controller::correctedText);
  auto text=QString("teh ").repeated(10000);first.fix(text);second.fix(text);
  QTRY_VERIFY_WITH_TIMEOUT(!first.busy()&&!second.busy(),15000);
  QCOMPARE(one.count()+two.count(),1);
  QVERIFY(first.notice().contains("running")||second.notice().contains("running"));
 }
 void immediateCancellationDoesNotApply(){
  QTemporaryDir data;reword::Controller controller(data.path());QSignalSpy results(&controller,&reword::Controller::correctedText);
  controller.fix(QString("teh ").repeated(10000));controller.cancel();
  QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),12000);QCOMPARE(results.count(),0);
 }
 void storageFailureDoesNotEscape(){
  QTemporaryDir data;reword::Controller controller(data.path());QTest::qWait(300);
  {
   auto db=QSqlDatabase::addDatabase("QSQLITE","storage-lock-test");db.setDatabaseName(data.path()+"/reword.sqlite");QVERIFY(db.open());
   QSqlQuery lock(db);QVERIFY(lock.exec("BEGIN EXCLUSIVE"));
   bool threw=false;try{controller.addWord("synthetic");}catch(...){threw=true;}
   QVERIFY(lock.exec("ROLLBACK"));db.close();
   QVERIFY2(!threw,"A database failure escaped the controller boundary.");
   QVERIFY(controller.notice().contains("storage",Qt::CaseInsensitive));
  }
  QSqlDatabase::removeDatabase("storage-lock-test");
  controller.addWord("synthetic");QVERIFY(controller.words().contains("synthetic"));
 }
 void workspaceCorrectionAndStaleGuard(){
  QTemporaryDir data;reword::Controller controller(data.path());
  QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("Backend",&controller);
  engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR)+"/qml/Main.qml"));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto root=engine.rootObjects().first();auto editor=root->findChild<QObject*>("workspaceEditor");QVERIFY(editor);
  editor->setProperty("text","im here with teh book");
  controller.fix("im here with teh book");
  QTRY_COMPARE_WITH_TIMEOUT(editor->property("text").toString(),QString("I am here with the book"),12000);
  QCOMPARE(controller.history().size(),1);
  QCOMPARE(controller.historyDetail(controller.history().first().toMap()["id"].toLongLong())["original"].toString(),QString("im here with teh book"));
  editor->setProperty("text","teh");controller.fix("teh");editor->setProperty("text","my newer text");
  QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),12000);
  QCOMPARE(editor->property("text").toString(),QString("my newer text"));QCOMPARE(controller.history().size(),1);
  // Every page must instantiate and render without losing its navigation state.
  for(int page=0;page<7;++page){root->setProperty("page",page);QTest::qWait(30);QCOMPARE(root->property("page").toInt(),page);}
 }
};
int main(int argc,char**argv){QApplication app(argc,argv);app.setOrganizationName("Reword");app.setApplicationName("RewordTests");QQuickStyle::setStyle("Basic");UiTest test;return QTest::qExec(&test,argc,argv);}
#include "ui_test.moc"
