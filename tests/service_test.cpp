#include "app/service.h"
#include <QtTest>
#include <functional>
#include <QSqlQuery>
#include <QUuid>
#include <QScopeGuard>
using namespace reword;

class FixtureAdapter final : public Adapter {
  public:
    Field field;
    int writes = 0, reads = 0, inspections = 0;
    bool rejectNext = false;
    QString unavailableFix;
    bool registrationAvailable = true;
    QStringList activeShortcuts;
    bool rejectRestore = false, rejectedShortcut = false;
    int rejectedShortcutAttempts = 0;
    std::function<void()> onShortcutRejected;
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
    bool shortcuts(const QString &fix, const QString &undo, const QString &pause) override {
        activeShortcuts.clear();
        if (!registrationAvailable || fix == unavailableFix ||
            (rejectRestore && rejectedShortcut)) {
            rejectedShortcut = true;
            ++rejectedShortcutAttempts;
            if (onShortcutRejected) {
                auto callback = std::exchange(onShortcutRejected, {});
                callback();
            }
            return false;
        }
        activeShortcuts = {fix, undo, pause};
        return true;
    }
};
struct WriterLock {
    QString name = QUuid::createUuid().toString();
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", name);
    explicit WriterLock(const QString &path) {
        database.setDatabaseName(path + "/reword.sqlite");
        database.open();
    }
    bool execute(const QString &sql) {
        QSqlQuery query(database);
        return query.exec(sql);
    }
    ~WriterLock() {
        execute("ROLLBACK");
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
};
struct Harness {
    QTemporaryDir directory;
    Store store{directory.path()};
    FixtureAdapter *adapter = new FixtureAdapter;
    Service service{directory.path(), std::unique_ptr<Adapter>(adapter)};
    explicit Harness(bool registrationAvailable = true) {
        adapter->registrationAvailable = registrationAvailable;
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
    void shortcutConflictRestoresSettingsAndRegistrations() {
        Harness h;
        QSignalSpy messages(&h.service, &Service::message);
        const QStringList original{"Ctrl+Alt+R", "Ctrl+Alt+Z", "Ctrl+Alt+Shift+P"};
        QTRY_COMPARE(h.adapter->activeShortcuts, original);
        auto revision = h.store.value("configRevision", 0).toLongLong();
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_VERIFY(h.adapter->rejectedShortcut);
        QTRY_COMPARE(h.store.value("fixShortcut").toString(), original[0]);
        QCOMPARE(h.adapter->activeShortcuts, original);
        QVERIFY(h.store.value("configRevision", 0).toLongLong() > revision);
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.last().first().toString().contains("restored", Qt::CaseInsensitive));
    }
    void shortcutConflictDoesNotOverwriteNewerConfiguration() {
        Harness h;
        const QStringList original{"Ctrl+Alt+R", "Ctrl+Alt+Z", "Ctrl+Alt+Shift+P"};
        QTRY_COMPARE(h.adapter->activeShortcuts, original);
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.adapter->onShortcutRejected = [&] { h.store.setValue("fixShortcut", "Ctrl+Alt+Y"); };
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        const QStringList newest{"Ctrl+Alt+Y", "Ctrl+Alt+Z", "Ctrl+Alt+Shift+P"};
        QTRY_COMPARE(h.adapter->activeShortcuts, newest);
        QCOMPARE(h.store.value("fixShortcut").toString(), newest[0]);
    }
    void failedShortcutSettingsWriteIsRetried() {
        Harness h;
        WriterLock writer(h.directory.path());
        QSignalSpy messages(&h.service, &Service::message);
        QTRY_VERIFY(!h.adapter->activeShortcuts.isEmpty());
        bool lockStarted = false;
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.adapter->onShortcutRejected = [&] { lockStarted = writer.execute("BEGIN IMMEDIATE"); };
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_VERIFY(lockStarted && !messages.isEmpty());
        QVERIFY(writer.execute("ROLLBACK"));
        QTRY_COMPARE(h.store.value("fixShortcut").toString(), QString("Ctrl+Alt+R"));
        QCOMPARE(h.adapter->activeShortcuts.first(), QString("Ctrl+Alt+R"));
    }
    void newerSettingsRevisionRequiresFreshRegistrationAttempt() {
        Harness h;
        QTRY_VERIFY(!h.adapter->activeShortcuts.isEmpty());
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.adapter->onShortcutRejected = [&] {
            h.store.setValue("fixShortcut", "Ctrl+Alt+Y");
            h.store.setValue("fixShortcut", "Ctrl+Alt+X");
            h.store.setValue("configRevision", h.store.value("configRevision", 0).toLongLong() + 1);
        };
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_COMPARE(h.store.value("fixShortcut").toString(), QString("Ctrl+Alt+R"));
        QVERIFY(h.adapter->rejectedShortcutAttempts >= 2);
    }
    void resavingUnavailableShortcutsRetriesRegistration() {
        Harness h;
        QTRY_VERIFY(!h.adapter->activeShortcuts.isEmpty());
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.adapter->rejectRestore = true;
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_COMPARE(h.store.value("fixShortcut").toString(), QString("Ctrl+Alt+R"));
        QVERIFY(h.adapter->activeShortcuts.isEmpty());
        h.adapter->rejectRestore = false;
        h.adapter->unavailableFix.clear();
        h.store.setValue("configRevision", h.store.value("configRevision", 0).toLongLong() + 1);
        const QStringList original{"Ctrl+Alt+R", "Ctrl+Alt+Z", "Ctrl+Alt+Shift+P"};
        QTRY_COMPARE(h.adapter->activeShortcuts, original);
    }
    void failedShortcutRestoreReportsUnavailable() {
        Harness h;
        QSignalSpy states(&h.service, &Service::state);
        QSignalSpy messages(&h.service, &Service::message);
        QTRY_VERIFY(!h.adapter->activeShortcuts.isEmpty());
        h.adapter->unavailableFix = "Ctrl+Alt+X";
        h.adapter->rejectRestore = true;
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_VERIFY(h.adapter->rejectedShortcut);
        QTRY_COMPARE(h.store.value("fixShortcut").toString(), QString("Ctrl+Alt+R"));
        QVERIFY(h.adapter->activeShortcuts.isEmpty());
        QVERIFY(!states.isEmpty());
        QCOMPARE(states.last()[1].toBool(), false);
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.last().first().toString().contains("unavailable", Qt::CaseInsensitive));
    }
    void unavailableWaylandGlobalShortcutsKeepFcitxConfiguration() {
        auto oldSession = qgetenv("XDG_SESSION_TYPE");
        qputenv("XDG_SESSION_TYPE", "wayland");
        auto restoreSession = qScopeGuard([&] {
            if (oldSession.isNull())
                qunsetenv("XDG_SESSION_TYPE");
            else
                qputenv("XDG_SESSION_TYPE", oldSession);
        });
        Harness h(false);
        QSignalSpy messages(&h.service, &Service::message);
        QTRY_VERIFY(h.adapter->rejectedShortcutAttempts > 0);
        auto attempts = h.adapter->rejectedShortcutAttempts;
        h.store.setValue("fixShortcut", "Ctrl+Alt+X");
        QTRY_VERIFY(h.adapter->rejectedShortcutAttempts > attempts);
        QCOMPARE(h.store.value("fixShortcut").toString(), QString("Ctrl+Alt+X"));
        QVERIFY(messages.isEmpty());
    }
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
