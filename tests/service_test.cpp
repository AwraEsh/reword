#include "app/service.h"
#include <QtTest>
using namespace reword;

class FixtureAdapter final : public Adapter {
  public:
    Field field;
    int writes = 0, reads = 0, inspections = 0;
    bool rejectNext = false;
    FixtureAdapter() {
        field.app = "synthetic-text-fixture";
        field.id = "entry";
        field.known = field.editable = field.focused = field.plain = true;
        field.locked = false;
        field.caret = 0;
    }
    void type(QString text) {
        field.text = text;
        field.caret = text.size();
        ++field.revision;
    }
    Field inspect(bool includeText) override {
        ++inspections;
        auto value = field;
        if (includeText)
            ++reads;
        else
            value.text.clear();
        return value;
    }
    bool replace(const Field &expected, const QString &text, int caret) override {
        ++writes;
        if (std::exchange(rejectNext, false) || !sameTarget(expected, field))
            return false;
        type(text);
        field.caret = caret;
        return true;
    }
    QString description() const override {
        return "Synthetic text fixture";
    }
    bool shortcuts(const QString &, const QString &, const QString &) override {
        return true;
    }
};
struct Harness {
    QTemporaryDir directory;
    Store store{directory.path()};
    FixtureAdapter *adapter = new FixtureAdapter;
    Service service{directory.path(), std::unique_ptr<Adapter>(adapter)};
    Harness() {
        store.discover(adapter->field.app, "Synthetic text fixture");
        store.decide(adapter->field.app, "allow");
        service.start();
    }
    ~Harness() {
        service.stop();
    }
};
class ServiceTest : public QObject {
    Q_OBJECT
  private slots:
    void fastTypingKeepsCompletedWord() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh alpha");
        QTRY_COMPARE(h.adapter->field.text, QString("the alpha"));
        QCOMPARE(h.adapter->field.caret, 9);
        QVERIFY(h.store.histories().isEmpty());
    }
    void burstCorrectsEachCompletedWord() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh recieve ");
        QTRY_COMPARE(h.adapter->field.text, QString("the receive "));
    }
    void burstPreservesOffsetsAndPunctuation() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("helllo, teh next");
        QTRY_COMPARE(h.adapter->field.text, QString("hello, the next"));
        QCOMPARE(h.adapter->field.caret, 15);
    }
    void alternatingRewritesAndFocusReset() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        h.adapter->type("");
        auto before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        h.adapter->type("teh ");
        before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        QCOMPARE(h.adapter->field.text, QString("teh "));
        h.adapter->type("");
        before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        h.adapter->field.id = "other-entry";
        h.adapter->type("");
        before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
    }
    void staleUndoPreservesNewText() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        h.adapter->type("the newer text");
        h.service.action("undo");
        QCOMPARE(h.adapter->field.text, QString("the newer text"));
        QCOMPARE(h.adapter->writes, 1);
    }
    void failedWriteDoesNotAdvanceAlternation() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->rejectNext = true;
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->writes, 1);
        QCOMPARE(h.adapter->field.text, QString("teh "));
        h.adapter->type("");
        auto before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
    }
    void protectedContextIsUntouched() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("https://teh.example/x user@teh.example `teh word` ");
        auto before = h.adapter->reads;
        QTRY_VERIFY(h.adapter->reads > before);
        QCOMPARE(h.adapter->writes, 0);
    }
    void passwordNeverRequestsText() {
        Harness h;
        h.adapter->field.password = true;
        h.adapter->type("synthetic-sensitive");
        QTRY_VERIFY(h.adapter->inspections > 1);
        QCOMPARE(h.adapter->reads, 0);
        QCOMPARE(h.adapter->writes, 0);
        QVERIFY(h.store.histories().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ServiceTest)
#include "service_test.moc"
