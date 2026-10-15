#include <QtTest>
#include "core/core.h"
#include "core/store.h"
using namespace reword;
class CoreTest: public QObject {
 Q_OBJECT
private slots:
 void engineLanguageAndProtection() {
  Engine e; Options o;
  QVERIFY(e.englishAvailable()); QVERIFY(e.persianAvailable());
  QCOMPARE(e.word("teh",o),QString("the"));
  QCOMPARE(e.word("TEH",o),QString("THE"));
  QCOMPARE(e.word("Teh",o),QString("The"));
  QCOMPARE(e.word("user@teh.com",o),QString("user@teh.com"));
  o.english=false; QCOMPARE(e.word("teh",o),QString("teh"));
  o.english=true; e.setPersonal({"teh"},{}); QCOMPARE(e.word("teh",o),QString("teh"));
 }
 void textRulesAndBounds() {
  Engine e; Options o; o.spelling=false;
  QCOMPARE(e.text("im here",o).corrected,QString("I am here"));
  o.abbreviations=false; QCOMPARE(e.text("im here",o).corrected,QString("im here"));
  o.capitalization=true; QCOMPARE(e.text("hello. world",o).corrected,QString("Hello. World"));
  o.persian=false; o.normalizePersian=true;
  QCOMPARE(e.text(QString::fromUtf8("كي"),o).corrected,QString::fromUtf8("كي"));
  o.persian=true; QCOMPARE(e.text(QString::fromUtf8("كي"),o).corrected,QString::fromUtf8("کی"));
  o.maxCharacters=3; auto tooBig=e.text("hello",o); QVERIFY(!tooBig.error.isEmpty()); QCOMPARE(tooBig.corrected,QString("hello"));
  std::atomic_bool cancel{true}; o.maxCharacters=50000; QVERIFY(!e.text("hello",o,&cancel).error.isEmpty());
  o.capitalization=false; o.periods=false; o.commas=false; o.spacing=true;
  QCOMPARE(e.text("hello  .. world ,,",o).corrected,QString("hello  .. world ,,"));
 }
 void persistenceConsentPauseAndRetention() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  Options o; o.historyCount=2; Store s(dir.path());
  s.discover("/app","App"); QVERIFY(!s.allowed("/app")); QVERIFY(s.shouldAsk("/app",QDate(2026,10,15)));
  s.decide("/app","later",QDate(2026,10,15));
  QVERIFY(!s.shouldAsk("/app",QDate(2026,10,15))); QVERIFY(s.shouldAsk("/app",QDate(2026,10,16)));
  s.decide("/app","deny"); QVERIFY(!s.shouldAsk("/app",QDate(2030,1,1)));
  s.decide("/app","allow"); QVERIFY(s.allowed("/app"));
  s.pause(10); QVERIFY(s.paused()); s.pause(0); QVERIFY(!s.paused());
  s.pause(-1); QVERIFY(s.paused()); s.pause(0);
  s.history("one","1",o,100); s.history("two","2",o,101); s.history("three","3",o,102);
  QCOMPARE(s.histories().size(),2); QCOMPARE(s.histories().last().toMap()["original"].toString(),QString("two"));
  o.history=false; s.history("four","4",o,103); QCOMPARE(s.histories().size(),2);
  o.history=true; o.historyDays=1; s.prune(o,100000); QCOMPARE(s.histories().size(),0);
  s.history(QString(600000,'a'),QString(600000,'b'),o,100001); o.historyMiB=1; s.prune(o,100002); QCOMPARE(s.histories().size(),0);
  s.count("teh","the",o); QVERIFY(s.statistics().isEmpty());
  o.statistics=true;s.count("teh","the",o);s.count("teh","the",o);
  QCOMPARE(s.statistics().first().toMap()["count"].toInt(),2);
 }

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
