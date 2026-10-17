#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <atomic>
#include "app/controller.h"
#include "platform/recovery_channel.h"
class BlockedReply : public QNetworkReply {
  public:
    BlockedReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        setError(QNetworkReply::ContentAccessDenied, "Test network blocked");
        setFinished(true);
        QTimer::singleShot(0, this, [this] { emit finished(); });
    }
    void abort() override {}
    qint64 readData(char *, qint64) override {
        return -1;
    }
};
class AuditNetwork : public QNetworkAccessManager {
    std::atomic_int &requests_;

  public:
    AuditNetwork(std::atomic_int &requests, QObject *parent)
        : QNetworkAccessManager(parent), requests_(requests) {}
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        if (request.url().scheme() == "http" || request.url().scheme() == "https")
            ++requests_;
        return new BlockedReply(request, this);
    }
};
class AuditFactory : public QQmlNetworkAccessManagerFactory {
    std::atomic_int &requests_;

  public:
    explicit AuditFactory(std::atomic_int &requests) : requests_(requests) {}
    QNetworkAccessManager *create(QObject *parent) override {
        return new AuditNetwork(requests_, parent);
    }
};
class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void savedMarkupNeverLoadsRemoteResources() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        controller.recordApplied("<img src='https://example.invalid/unsafe'> teh",
                                 "<img src='https://example.invalid/unsafe'> the");
        std::atomic_int requests{0};
        AuditFactory factory(requests);
        QQmlApplicationEngine engine;
        engine.setNetworkAccessManagerFactory(&factory);
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        engine.rootObjects().first()->setProperty("page", 4);
        QTest::qWait(300);
        QCOMPARE(requests.load(), 0);
    }
    void originalCopySurvivesStaleWorkspaceUndoWithHistoryOff() {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Clipboard testing requires the isolated offscreen platform.");
        QTemporaryDir data;
        reword::Controller controller(data.path());
        controller.set("history", false);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto editor = root->findChild<QObject *>("workspaceEditor");
        QVERIFY(editor);
        editor->setProperty("text", "im here with teh book");
        controller.fix("im here with teh book");
        QTRY_COMPARE_WITH_TIMEOUT(editor->property("text").toString(),
                                  QString("I am here with the book"), 12000);
        editor->setProperty("text", "my subsequent edits");
        QCOMPARE(controller.history().size(), 0);
        auto copy = root->findChild<QObject *>("copyRecoveryOriginal");
        QVERIFY2(copy, "The original needs an explicit Copy action after Undo becomes stale.");
        QVERIFY(copy->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(copy, "clicked"));
        QCOMPARE(QApplication::clipboard()->text(), QString("im here with teh book"));
        QCOMPARE(editor->property("text").toString(), QString("my subsequent edits"));
        QCOMPARE(controller.history().size(), 0);
    }
    void privateRecoveryReachesControllerWithoutHistoryOrClipboard() {
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Clipboard testing requires the isolated offscreen platform.");
        QGuiApplication::clipboard()->setText("unrelated clipboard sentinel");
        QTemporaryDir data;
        reword::RecoveryClient client;
        {
            reword::Controller controller(data.path());
            controller.set("history", false);
            client.start(data.path());
            QTRY_VERIFY_WITH_TIMEOUT(client.active(), 2500);
            auto original = QString::fromUtf8("im here متن فارسی");
            QVERIFY(client.submit(original, "corrected"));
            QTRY_COMPARE(controller.recoveryOriginal(), original);
            QCOMPARE(controller.recoveryCorrected(), QString("corrected"));
            QCOMPARE(controller.history().size(), 0);
            QCOMPARE(QGuiApplication::clipboard()->text(), QString("unrelated clipboard sentinel"));
        }
        QTRY_VERIFY(!client.active());
        QVERIFY(!client.submit("after app closes", "result"));
    }
    void operationLimitCannotBeRaisedAboveFiftyThousand() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        controller.set("maxCharacters", 100000);
        QCOMPARE(controller.settings()["maxCharacters"].toInt(), 50000);
        QSignalSpy corrected(&controller, &reword::Controller::correctedText);
        controller.fix(QString(50001, 'x'));
        QVERIFY(!controller.busy());
        QCOMPARE(corrected.count(), 0);
        QVERIFY(controller.notice().contains("limit"));
    }
    void sessionRecoveryIsBoundedAndNotPersisted() {
        QTemporaryDir data;
        {
            reword::Controller controller(data.path());
            controller.set("history", false);
            controller.recordApplied("teh", "the");
            QCOMPARE(controller.property("recoveryOriginal").toString(), QString("teh"));
            controller.recordApplied(QString(50001, 'x'), "short");
            QCOMPARE(controller.property("recoveryOriginal").toString(), QString("teh"));
            auto unicode = QString::fromUcs4(U"🙂").repeated(50000);
            controller.recordApplied(unicode, QString(50000, 'a'));
            QCOMPARE(controller.property("recoveryOriginal").toString(), unicode);
            controller.recordApplied("short", unicode + "x");
            QCOMPARE(controller.property("recoveryOriginal").toString(), unicode);
            QCOMPARE(controller.history().size(), 0);
            QVERIFY(QMetaObject::invokeMethod(&controller, "clearRecovery"));
            QVERIFY(controller.property("recoveryOriginal").toString().isEmpty());
            controller.recordApplied("new original", "new result");
        }
        reword::Controller reopened(data.path());
        QVERIFY(reopened.property("recoveryOriginal").toString().isEmpty());
        QCOMPARE(reopened.history().size(), 0);
    }
    void passageLockRejectsConcurrentWork() {
        QTemporaryDir data;
        reword::Controller first(data.path()), second(data.path());
        QSignalSpy one(&first, &reword::Controller::correctedText),
            two(&second, &reword::Controller::correctedText);
        auto text = QString("teh ").repeated(10000);
        first.fix(text);
        second.fix(text);
        QTRY_VERIFY_WITH_TIMEOUT(!first.busy() && !second.busy(), 15000);
        QCOMPARE(one.count() + two.count(), 1);
        QVERIFY(first.notice().contains("running") || second.notice().contains("running"));
    }
    void pausePreventsPendingAndNewCorrections() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        QSignalSpy results(&controller, &reword::Controller::correctedText);
        controller.fix(QString("teh ").repeated(10000));
        controller.pause(-1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 12000);
        QCOMPARE(results.count(), 0);
        controller.fix("teh");
        QTest::qWait(300);
        QVERIFY(!controller.busy());
        QCOMPARE(results.count(), 0);
    }
    void immediateCancellationDoesNotApply() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        QSignalSpy results(&controller, &reword::Controller::correctedText);
        controller.fix(QString("teh ").repeated(10000));
        controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 12000);
        QCOMPARE(results.count(), 0);
    }
    void storageFailureDoesNotEscape() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        QTest::qWait(300);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "storage-lock-test");
            db.setDatabaseName(data.path() + "/reword.sqlite");
            QVERIFY(db.open());
            QSqlQuery lock(db);
            QVERIFY(lock.exec("BEGIN EXCLUSIVE"));
            bool threw = false;
            try {
                controller.addWord("synthetic");
            } catch (...) {
                threw = true;
            }
            QVERIFY(lock.exec("ROLLBACK"));
            db.close();
            QVERIFY2(!threw, "A database failure escaped the controller boundary.");
            QVERIFY(controller.notice().contains("storage", Qt::CaseInsensitive));
        }
        QSqlDatabase::removeDatabase("storage-lock-test");
        controller.addWord("synthetic");
        QVERIFY(controller.words().contains("synthetic"));
    }
    void workspaceCorrectionAndStaleGuard() {
        QTemporaryDir data;
        reword::Controller controller(data.path());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto editor = root->findChild<QObject *>("workspaceEditor");
        QVERIFY(editor);
        editor->setProperty("text", "previous valid text");
        editor->setProperty("text", QString(50001, 'x'));
        QCOMPARE(editor->property("text").toString(), QString("previous valid text"));
        QCOMPARE(controller.characterCount(QString::fromUcs4(U"🙂")), 1);
        editor->setProperty("text", "im here with teh book");
        controller.fix("im here with teh book");
        QTRY_COMPARE_WITH_TIMEOUT(editor->property("text").toString(),
                                  QString("I am here with the book"), 12000);
        QCOMPARE(controller.history().size(), 1);
        QCOMPARE(
            controller
                .historyDetail(controller.history().first().toMap()["id"].toLongLong())["original"]
                .toString(),
            QString("im here with teh book"));
        editor->setProperty("text", "teh");
        controller.fix("teh");
        editor->setProperty("text", "my newer text");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 12000);
        QCOMPARE(editor->property("text").toString(), QString("my newer text"));
        QCOMPARE(controller.history().size(), 1);
        // Optional synthetic-only captures for visual review; never production data.
        const auto captureDirectory = qEnvironmentVariable("REWORD_UI_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            auto window = qobject_cast<QQuickWindow *>(root);
            QVERIFY(window);
            if (qEnvironmentVariableIsSet("REWORD_UI_CAPTURE_COMPACT")) {
                window->setWidth(880);
                window->setHeight(620);
            }
            editor->setProperty(
                "text",
                QString::fromUtf8("A short English paragraph.\nیک متن فارسی برای بررسی نمایش."));
            for (bool dark : {true, false}) {
                controller.set("dark", dark);
                for (int page = 0; page < 7; ++page) {
                    root->setProperty("page", page);
                    QTest::qWait(100);
                    auto image = window->grabWindow();
                    QVERIFY(!image.isNull());
                    QVERIFY(
                        image.save(captureDirectory +
                                   QString("/%1-%2.png").arg(dark ? "dark" : "light").arg(page)));
                }
            }
        }
        // Every page must instantiate and render without losing its navigation state.
        for (int page = 0; page < 7; ++page) {
            root->setProperty("page", page);
            QTest::qWait(30);
            QCOMPARE(root->property("page").toInt(), page);
        }
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Reword");
    app.setApplicationName("RewordTests");
    QQuickStyle::setStyle("Basic");
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_test.moc"
