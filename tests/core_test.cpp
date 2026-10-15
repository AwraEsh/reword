#include <QtTest>
#include "core/core.h"
#include "core/store.h"
using namespace reword;
class CoreTest: public QObject {
 Q_OBJECT
private slots:
 void unsafeFieldsNeverPass() {
  Field f{"app","field","hello",5,-1,-1,true,false,true,true,false,true,1};
  Options o; Access a{true,false,false};
  QVERIFY(safe(f,a,o));
  f.password=true; QVERIFY(!safe(f,a,o)); f.password=false;
  f.known=false; QVERIFY(!safe(f,a,o)); f.known=true;
  f.locked=true; QVERIFY(!safe(f,a,o)); f.locked=false;
  f.focused=false; QVERIFY(!safe(f,a,o)); f.focused=true;
  a.allowed=false; QVERIFY(!safe(f,a,o)); a.allowed=true;
  a.paused=true; QVERIFY(!safe(f,a,o)); a.paused=false;
  a.browser=true; o.browsers=false; QVERIFY(!safe(f,a,o));
 }
 void retryAlternatesAndResets() {
  RetryCycle c;
  QVERIFY(c.shouldCorrect("f",4,"teh",100));
  QVERIFY(!c.shouldCorrect("f",4,"teh",200));
  QVERIFY(c.shouldCorrect("f",4,"teh",300));
  QVERIFY(c.shouldCorrect("g",4,"teh",400));
  QVERIFY(c.shouldCorrect("g",8,"teh",500));
  QVERIFY(c.shouldCorrect("g",8,"teh",31000));
  c.reset(); QVERIFY(c.shouldCorrect("g",8,"teh",31001));
 }
 void staleTargetRejected() {
  Field f{"app","f","text",4,-1,-1,true,false,true,true,false,true,1};
  Field g=f; QVERIFY(sameTarget(f,g));
  g.text="edited"; QVERIFY(!sameTarget(f,g)); g=f;
  g.caret=1; QVERIFY(!sameTarget(f,g)); g=f;
  g.id="another"; QVERIFY(!sameTarget(f,g)); g=f;
  g.password=true; QVERIFY(!sameTarget(f,g));
 }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
