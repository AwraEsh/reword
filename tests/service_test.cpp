#include "app/service.h"
#include <QtTest>
#include <functional>
#include <stdexcept>
#include <QSqlQuery>
#include <QUuid>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QtConcurrent>
using namespace reword;

namespace reword {
struct ServiceTestAccess {
    static bool cancellationRequested(const Service &service) {
        return service.cancel_.load();
    }
    static bool retainsTransientText(const Service &service) {
        return !service.previous_.text.isEmpty() || !service.undoAfter_.text.isEmpty() ||
               !service.undoBefore_.isEmpty() || !service.retryPrefix_.isEmpty() ||
               !service.retryWord_.isEmpty();
    }
};
}

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
    std::function<void()> onShortcutRejected, onRead, onWrite, onMetadata;
    bool quiet = false;
    QString pendingAction;
    QString takeAction() override {
        return pendingAction;
    }
    bool changed() override {
        return !quiet;
    }
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
        if (includeText) {
            ++reads;
            if (onRead) {
                auto callback = std::exchange(onRead, {});
                callback();
            }
        } else {
            value.text.clear();
            if (auto callback = std::exchange(onMetadata, {}))
                callback();
        }
        return value;
    }
    Field readExpected(const Field &expected) override {
        if (expected.app != field.app || expected.id != field.id || field.password ||
            !field.known || !field.editable || !field.focused || field.locked || !field.plain)
            return {};
        return inspect(true);
    }
    bool replace(const Field &expected, const QString &text, int caret) override {
        ++writes;
        if (std::exchange(rejectNext, false) || !sameTarget(expected, field))
            return false;
        type(text);
        field.caret = caret;
        if (auto callback = std::exchange(onWrite, {}))
            callback();
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
// Hold only this test process's worker pool; release before Service teardown,
// including on assertion failures, so a queued correction cannot deadlock stop.
struct BlockedWorkerPool {
    QThreadPool *pool = QThreadPool::globalInstance();
    int previousMaximum = pool->maxThreadCount();
    QSemaphore started, release;
    QFuture<void> blocker;
    bool released = false;
    BlockedWorkerPool() {
        pool->waitForDone();
        pool->setMaxThreadCount(1);
        blocker = QtConcurrent::run([this] {
            started.release();
            release.acquire();
        });
    }
    bool ready() {
        return started.tryAcquire(1, 2000);
    }
    void unblock() {
        if (!released) {
            released = true;
            release.release();
            blocker.waitForFinished();
        }
    }
    ~BlockedWorkerPool() {
        unblock();
        pool->waitForDone();
        pool->setMaxThreadCount(previousMaximum);
    }
};
class ServiceTest : public QObject {
    Q_OBJECT
  private slots:
    void postWriteReadStaysOnThePermittedTarget_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("transition");
        for (auto operation : {"live", "selection", "undo"})
            for (auto transition : {"application", "field", "password", "unknown"}) {
                auto name = QString(operation) + '-' + transition;
                QTest::newRow(name.toUtf8().constData()) << QString(operation) << QString(transition);
            }
    }
    void postWriteReadStaysOnThePermittedTarget() {
        QFETCH(QString, operation);
        QFETCH(QString, transition);
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        if (operation == "undo") {
            h.adapter->type("teh ");
            QTRY_COMPARE(h.adapter->field.text, QString("the "));
        }
        h.adapter->quiet = operation != "live";
        int unexpectedReads = 0;
        h.adapter->onWrite = [&] {
            h.adapter->quiet = true;
            if (transition == "application")
                h.adapter->field.app = "synthetic-unapproved-app";
            else if (transition == "field")
                h.adapter->field.id = "another-synthetic-field";
            else if (transition == "password")
                h.adapter->field.password = true;
            else
                h.adapter->field.known = false;
            h.adapter->onRead = [&] { ++unexpectedReads; };
        };
        QSignalSpy recovery(&h.service, &Service::passageRecoveryAvailable);
        const auto writes = h.adapter->writes;
        if (operation == "live")
            h.adapter->type("teh ");
        else if (operation == "selection") {
            h.adapter->type("teh");
            h.adapter->field.selectionStart = 0;
            h.adapter->field.selectionEnd = 3;
            h.service.action("fix");
        } else
            h.service.action("undo");
        QTRY_COMPARE_WITH_TIMEOUT(h.adapter->writes, writes + 1, 12000);
        QCOMPARE(unexpectedReads, 0);
        QVERIFY(!ServiceTestAccess::retainsTransientText(h.service));
        if (operation == "selection") {
            QCOMPARE(recovery.size(), 1);
            QCOMPARE(recovery.first()[0].toString(), QString("teh"));
            QVERIFY(h.store.histories().isEmpty());
        }
    }
    void unconfirmedSelectionPreservesSessionOriginal() {
        Harness h;
        h.adapter->quiet = true;
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        h.adapter->rejectNext = true;
        QSignalSpy recovery(&h.service, &Service::passageRecoveryAvailable);
        QSignalSpy messages(&h.service, &Service::message);
        h.service.action("fix");
        QTRY_COMPARE_WITH_TIMEOUT(h.adapter->writes, 1, 12000);
        QCOMPARE(recovery.size(), 1);
        QCOMPARE(recovery.first()[0].toString(), QString("teh"));
        QCOMPARE(h.adapter->field.text, QString("teh"));
        QVERIFY(h.store.histories().isEmpty());
        QVERIFY(!messages.isEmpty());
    }
    void unsafeBoundaryReleasesTransientText_data() {
        QTest::addColumn<QString>("boundary");
        for (auto value : {"password", "locked", "unknown", "denied", "paused", "exception", "stop"})
            QTest::newRow(value) << QString(value);
    }
    void unsafeBoundaryReleasesTransientText() {
        QFETCH(QString, boundary);
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("hello teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("hello the "));
        QVERIFY(ServiceTestAccess::retainsTransientText(h.service));
        if (boundary == "stop") {
            h.service.stop();
        } else {
            const auto inspections = h.adapter->inspections;
            const auto reads = h.adapter->reads;
            if (boundary == "password")
                h.adapter->field.password = true;
            else if (boundary == "locked")
                h.adapter->field.locked = true;
            else if (boundary == "unknown")
                h.adapter->field.known = false;
            else if (boundary == "denied")
                h.store.decide(h.adapter->field.app, "deny");
            else if (boundary == "paused")
                h.store.pause(-1);
            else
                h.adapter->onRead = [&] {
                    // Observe this failure boundary before a later permitted
                    // timer tick legitimately caches the safe field again.
                    h.adapter->quiet = true;
                    throw std::runtime_error("Synthetic inspection failure");
                };
            QTRY_VERIFY(h.adapter->inspections > inspections);
            if (boundary != "exception")
                QCOMPARE(h.adapter->reads, reads);
        }
        QVERIFY(!ServiceTestAccess::retainsTransientText(h.service));
    }
    void inspectionFailureInvalidatesUndo() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        QSignalSpy messages(&h.service, &Service::message);
        h.adapter->onRead = [] { throw std::runtime_error("Synthetic inspection failure"); };
        QTRY_VERIFY(!messages.isEmpty());
        h.adapter->quiet = true;
        const auto writes = h.adapter->writes;
        h.service.action("undo");
        QCOMPARE(h.adapter->writes, writes);
        QCOMPARE(h.adapter->field.text, QString("the "));
    }
    void unsafeNativeJobRequestsCancellation_data() {
        QTest::addColumn<QString>("boundary");
        for (auto boundary : {"password", "unknown", "locked", "deny", "pause", "field",
                              "application", "exception"})
            QTest::newRow(boundary) << QString(boundary);
    }
    void unsafeNativeJobRequestsCancellation() {
        QFETCH(QString, boundary);
        Harness h;
        h.adapter->quiet = true;
        BlockedWorkerPool workers;
        QVERIFY(workers.ready());
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        QSignalSpy results(&h.service, &Service::result);
        h.service.action("fix");
        QVERIFY(!ServiceTestAccess::cancellationRequested(h.service));
        const auto reads = h.adapter->reads;
        if (boundary == "password")
            h.adapter->field.password = true;
        else if (boundary == "unknown")
            h.adapter->field.known = false;
        else if (boundary == "locked")
            h.adapter->field.locked = true;
        else if (boundary == "deny")
            h.store.decide(h.adapter->field.app, "deny");
        else if (boundary == "pause")
            h.store.pause(-1);
        else if (boundary == "field")
            h.adapter->field.id = "different-field";
        else if (boundary == "application")
            h.adapter->field.app = "different-unapproved-fixture";
        bool inspected = false;
        h.adapter->onMetadata = [&] {
            inspected = true;
            h.adapter->quiet = true;
            if (boundary == "exception")
                throw std::runtime_error("Synthetic metadata failure");
        };
        h.adapter->quiet = false;
        QTRY_VERIFY(inspected);
        QVERIFY(ServiceTestAccess::cancellationRequested(h.service));
        QCOMPARE(h.adapter->reads, reads);
        workers.unblock();
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 12000);
        QVERIFY(!results.first()[2].toString().isEmpty());
        QCOMPARE(h.adapter->writes, 0);
        QVERIFY(h.store.histories().isEmpty());
    }
    void unsafeExternalFieldPreservesWorkspaceJob() {
        Harness h;
        h.adapter->quiet = true;
        BlockedWorkerPool workers;
        QVERIFY(workers.ready());
        QSignalSpy results(&h.service, &Service::result);
        h.service.processText("teh");
        h.adapter->field.password = true;
        bool inspected = false;
        h.adapter->onMetadata = [&] {
            inspected = true;
            h.adapter->quiet = true;
        };
        h.adapter->quiet = false;
        QTRY_VERIFY(inspected);
        QVERIFY(!ServiceTestAccess::cancellationRequested(h.service));
        workers.unblock();
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 12000);
        QCOMPARE(results.first()[1].toString(), QString("the"));
        QVERIFY(results.first()[2].toString().isEmpty());
        QVERIFY(!results.first()[3].toBool());
        QCOMPARE(h.adapter->writes, 0);
    }
    void cancelledPassageDoesNotPoisonTheNextOperation() {
        Harness h;
        h.adapter->quiet = true;
        QSignalSpy results(&h.service, &Service::result);
        h.service.processText(QString("teh ").repeated(12500));
        h.service.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 12000);
        QVERIFY(!results.last()[2].toString().isEmpty());
        h.service.prepare();
        h.service.processText("teh");
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 12000);
        QCOMPARE(results.last()[1].toString(), QString("the"));
        QVERIFY(results.last()[2].toString().isEmpty());
    }
    void consecutivePassagesUseCurrentPersonalConfiguration() {
        Harness h;
        h.adapter->quiet = true;
        QSignalSpy results(&h.service, &Service::result);
        auto check = [&](const QString &expected) {
            const auto before = results.size();
            h.service.processText("teh");
            QTRY_COMPARE_WITH_TIMEOUT(results.size(), before + 1, 12000);
            QCOMPARE(results.last()[1].toString(), expected);
            QVERIFY(results.last()[2].toString().isEmpty());
        };
        check("the");
        h.store.addWord("teh");
        check("teh");
        h.store.removeWord("teh");
        check("the");
        h.store.replacement("teh", "book");
        check("book");
        h.store.replacement("teh", "");
        check("the");
        h.store.setValue("english", false);
        check("teh");
        h.store.setValue("english", true);
        check("the");
    }
    void pendingShortcutStillClearsUnsafeUndo_data() {
        QTest::addColumn<QString>("boundary");
        for (auto value : {"password", "locked", "unknown", "denied", "paused"})
            QTest::newRow(value) << QString(value);
    }
    void pendingShortcutStillClearsUnsafeUndo() {
        QFETCH(QString, boundary);
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        const auto saved = h.adapter->field;
        h.adapter->pendingAction = "pending";
        h.adapter->field.focused = false;
        if (boundary == "password")
            h.adapter->field.password = true;
        if (boundary == "locked")
            h.adapter->field.locked = true;
        if (boundary == "unknown")
            h.adapter->field.known = false;
        if (boundary == "denied")
            h.store.decide(saved.app, "deny");
        if (boundary == "paused")
            h.store.pause(-1);
        const auto reads = h.adapter->reads;
        QTest::qWait(100);
        QCOMPARE(h.adapter->reads, reads);
        h.adapter->field = saved;
        h.store.decide(saved.app, "allow");
        h.store.pause(0);
        h.adapter->pendingAction.clear();
        h.service.action("undo");
        QCOMPARE(h.adapter->field.text, QString("the "));
        QCOMPARE(h.adapter->writes, 1);
    }
    void pendingShortcutPreservesUndoAcrossGrabFocusLoss() {
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        h.adapter->pendingAction = "pending";
        h.adapter->field.focused = false;
        const auto reads = h.adapter->reads;
        QTest::qWait(100);
        QCOMPARE(h.adapter->reads, reads);
        h.adapter->field.focused = true;
        h.adapter->pendingAction.clear();
        h.service.action("undo");
        QCOMPARE(h.adapter->field.text, QString("teh "));
    }
    void confirmedPassageIsRecoverableWithoutHistory() {
        Harness h;
        h.store.setValue("history", false);
        QSignalSpy recovery(&h.service, SIGNAL(passageRecoveryAvailable(QString, QString)));
        QVERIFY(recovery.isValid());
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        h.service.action("fix");
        QTRY_COMPARE_WITH_TIMEOUT(recovery.count(), 1, 12000);
        QCOMPARE(recovery.first().at(0).toString(), QString("teh"));
        QCOMPARE(recovery.first().at(1).toString(), QString("the"));
        QCOMPARE(h.store.histories().size(), 0);
        h.adapter->type("subsequent user edit");
        auto writes = h.adapter->writes;
        h.service.action("undo");
        QCOMPARE(h.adapter->field.text, QString("subsequent user edit"));
        QCOMPARE(h.adapter->writes, writes);
        QCOMPARE(recovery.count(), 1);
    }
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
    void liveBurstStopsWhenPolicyChanges_data() {
        QTest::addColumn<QString>("policy");
        for (auto policy : {"pause", "deny", "language", "cancel", "cancel-reset"})
            QTest::newRow(policy) << QString(policy);
    }
    void liveBurstStopsWhenPolicyChanges() {
        QFETCH(QString, policy);
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        QSignalSpy corrections(&h.service, &Service::corrected);
        connect(&h.service, &Service::corrected, &h.service, [&] {
            if (corrections.count() != 1)
                return;
            if (policy == "pause")
                h.store.pause(-1);
            else if (policy == "deny")
                h.store.decide(h.adapter->field.app, "deny");
            else if (policy == "language") {
                h.store.setValue("english", false);
                h.store.setValue("configRevision",
                                 h.store.value("configRevision", 0).toLongLong() + 1);
            } else {
                h.service.cancel();
                if (policy == "cancel-reset")
                    h.service.prepare();
            }
        });
        h.adapter->type("teh recieve ");
        QTRY_VERIFY(!corrections.isEmpty());
        QCOMPARE(h.adapter->field.text, QString("the recieve "));
        QCOMPARE(h.adapter->writes, 1);
    }
    void selectionStopsWhenPolicyChangesDuringFinalRead_data() {
        liveBurstStopsWhenPolicyChanges_data();
    }
    void selectionStopsWhenPolicyChangesDuringFinalRead() {
        QFETCH(QString, policy);
        Harness h;
        h.adapter->quiet = true;
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        QSignalSpy results(&h.service, &Service::result);
        QSignalSpy messages(&h.service, &Service::message);
        h.service.action("fix");
        bool injected = false;
        h.adapter->onRead = [&] {
            injected = true;
            if (policy == "pause")
                h.store.pause(-1);
            else if (policy == "deny")
                h.store.decide(h.adapter->field.app, "deny");
            else if (policy == "language") {
                h.store.setValue("english", false);
                h.store.setValue("configRevision",
                                 h.store.value("configRevision", 0).toLongLong() + 1);
            } else {
                h.service.cancel();
                if (policy == "cancel-reset")
                    h.service.prepare();
            }
        };
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !messages.isEmpty(), 12000);
        QVERIFY(injected);
        QCOMPARE(h.adapter->field.text, QString("teh"));
        QCOMPARE(h.adapter->writes, 0);
        QVERIFY(h.store.histories().isEmpty());
    }
    void changedMetadataTargetIsNotRead_data() {
        QTest::addColumn<QString>("boundary");
        for (auto boundary : {"application", "field", "password", "unknown"})
            QTest::newRow(boundary) << QString(boundary);
    }
    void changedMetadataTargetIsNotRead() {
        QFETCH(QString, boundary);
        Harness h;
        h.adapter->quiet = true;
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        h.adapter->onMetadata = [&] {
            if (boundary == "application")
                h.adapter->field.app = "different-unapproved-fixture";
            else if (boundary == "field")
                h.adapter->field.id = "different-field";
            else if (boundary == "password")
                h.adapter->field.password = true;
            else
                h.adapter->field.known = false;
        };
        h.service.action("fix");
        QCOMPARE(h.adapter->reads, 0);
        QCOMPARE(h.adapter->writes, 0);
        QVERIFY(h.store.histories().isEmpty());
        QVERIFY(!ServiceTestAccess::retainsTransientText(h.service));
    }
    void recoveryPreparationHonorsChangedPolicy_data() {
        liveBurstStopsWhenPolicyChanges_data();
    }
    void recoveryPreparationHonorsChangedPolicy() {
        QFETCH(QString, policy);
        Harness h;
        h.adapter->quiet = true;
        h.adapter->type("teh");
        h.adapter->field.selectionStart = 0;
        h.adapter->field.selectionEnd = 3;
        QSignalSpy recovery(&h.service, &Service::passageRecoveryAvailable);
        QSignalSpy messages(&h.service, &Service::message);
        connect(&h.service, &Service::passageRecoveryAvailable, &h.service, [&] {
            if (policy == "pause")
                h.store.pause(-1);
            else if (policy == "deny")
                h.store.decide(h.adapter->field.app, "deny");
            else if (policy == "language") {
                h.store.setValue("english", false);
                h.store.setValue("configRevision",
                                 h.store.value("configRevision", 0).toLongLong() + 1);
            } else {
                h.service.cancel();
                if (policy == "cancel-reset")
                    h.service.prepare();
            }
        });
        h.service.action("fix");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 12000);
        QCOMPARE(recovery.count(), 1);
        QCOMPARE(h.adapter->writes, 0);
        QCOMPARE(h.adapter->field.text, QString("teh"));
        QVERIFY(h.store.histories().isEmpty());
    }
    void undoStopsWhenPolicyChangesDuringFinalRead_data() {
        liveBurstStopsWhenPolicyChanges_data();
    }
    void undoStopsWhenPolicyChangesDuringFinalRead() {
        QFETCH(QString, policy);
        Harness h;
        QTRY_VERIFY(h.adapter->reads > 0);
        h.adapter->type("teh ");
        QTRY_COMPARE(h.adapter->field.text, QString("the "));
        h.adapter->quiet = true;
        h.adapter->onRead = [&] {
            if (policy == "pause")
                h.store.pause(-1);
            else if (policy == "deny")
                h.store.decide(h.adapter->field.app, "deny");
            else if (policy == "language") {
                h.store.setValue("english", false);
                h.store.setValue("configRevision",
                                 h.store.value("configRevision", 0).toLongLong() + 1);
            } else {
                h.service.cancel();
                if (policy == "cancel-reset")
                    h.service.prepare();
            }
        };
        h.service.action("undo");
        QCOMPARE(h.adapter->field.text, QString("the "));
        QCOMPARE(h.adapter->writes, 1);
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
