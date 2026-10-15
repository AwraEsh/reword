#include <QtTest>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include "app/controller.h"
class UiTest:public QObject {
 Q_OBJECT
private slots:
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
