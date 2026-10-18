#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <algorithm>
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
class UiMetadataAdapter final : public reword::Adapter {
    QMutex mutex_;
    QString application_;

  public:
    void application(QString id) {
        QMutexLocker lock(&mutex_);
        application_ = std::move(id);
    }
    reword::Field inspect(bool) override {
        QMutexLocker lock(&mutex_);
        reword::Field field;
        field.app = application_;
        field.id = "controlled-ui-field";
        return field;
    }
    bool replace(const reword::Field &, const QString &, int) override {
        return false;
    }
    QString description() const override {
        return "Controlled UI metadata fixture";
    }
};
class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void dialogReceivesKeyboardFocus_data() {
        QTest::addColumn<QString>("title");
        for (auto title :
             {"Pause Reword", "Saved correction", "Delete all saved text?", "Practice spelling"})
            QTest::newRow(title) << QString(title);
    }
    void dialogReceivesKeyboardFocus() {
        QFETCH(QString, title);
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        auto editor = root->findChild<QQuickItem *>("workspaceEditor");
        QVERIFY(editor);
        editor->forceActiveFocus();
        QObject *dialog = nullptr;
        for (auto object : root->findChildren<QObject *>())
            if (object->property("title").toString() == title) {
                dialog = object;
                break;
            }
        QVERIFY(dialog);
        if (title == "Practice spelling")
            dialog->setProperty("answer", "receive");
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto content = dialog->property("contentItem").value<QQuickItem *>();
        QVERIFY(content && content->parentItem());
        const auto containsFocus = [&] {
            auto focused = window->activeFocusItem();
            auto popup = content->parentItem();
            return focused && (focused == popup || popup->isAncestorOf(focused));
        };
        QVERIFY2(containsFocus(), "Opening a dialog must move keyboard focus into its popup.");
        auto header = dialog->property("header").value<QQuickItem *>();
        auto background = dialog->property("background").value<QQuickItem *>();
        QVERIFY(header && background);
        const auto luminance = [](const QColor &color) {
            const auto linear = [](double value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
                   0.0722 * linear(color.blueF());
        };
        for (bool dark : {true, false}) {
            controller.set("dark", dark);
            QTest::qWait(50);
            const auto foreground = header->property("color").value<QColor>();
            const auto surface = background->property("color").value<QColor>();
            QVERIFY(foreground.isValid() && surface.isValid());
            auto first = luminance(foreground), second = luminance(surface);
            auto contrast = (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
            QVERIFY2(contrast >= 4.5, qPrintable(QString("Dialog heading contrast %1 in %2 theme")
                                                     .arg(contrast)
                                                     .arg(dark ? "dark" : "light")));
        }

        for (int i = 0; i < 20; ++i) {
            QTest::keyClick(window, Qt::Key_Tab);
            if (!containsFocus()) {
                auto focused = window->activeFocusItem();
                auto popupWindow = content->window();
                auto popupFocus = popupWindow ? popupWindow->activeFocusItem() : nullptr;
                const auto description = [](QQuickItem *item) {
                    return item ? QString("%1 text=%2")
                                      .arg(item->metaObject()->className(),
                                           item->property("text").toString())
                                : QString("null");
                };
                const auto captures = qEnvironmentVariable("REWORD_DIALOG_CAPTURE_DIR");
                if (!captures.isEmpty()) {
                    QDir().mkpath(captures);
                    auto name = title;
                    name.replace(' ', '_').remove('?');
                    window->grabWindow().save(captures + "/failure-focus-" + name + ".png");
                }
                QFAIL(qPrintable(
                    QString(
                        "Tab %1 escaped modal: root=%2 popup=%3 sameWindow=%4 opened=%5 pending=%6")
                        .arg(i)
                        .arg(description(focused), description(popupFocus))
                        .arg(popupWindow == window)
                        .arg(dialog->property("opened").toBool())
                        .arg(controller.pendingName())));
            }
        }
        const auto captures = qEnvironmentVariable("REWORD_DIALOG_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            window->setWidth(880);
            window->setHeight(620);
            root->setProperty(
                "selectedHistory",
                QVariantMap{
                    {"original",
                     QString::fromUtf8("im here. یک متن فارسی برای نمایش.\n").repeated(100)},
                    {"corrected",
                     QString::fromUtf8("I am here. یک متن فارسی برای نمایش.\n").repeated(100)}});
            for (bool dark : {false, true}) {
                controller.set("dark", dark);
                QTest::qWait(100);
                auto name = title;
                name.replace(' ', '_').remove('?');
                QVERIFY(window->grabWindow().save(captures + "/" + (dark ? "dark-" : "light-") +
                                                  name + ".png"));
            }
        }
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("opened").toBool());
    }
    void sidebarCanBeNavigatedWithoutPointer() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QQuickItem *overview = nullptr;
        // Repeater delegates belong to the visual tree; QObject ownership can differ.
        auto items = window->contentItem()->childItems();
        while (!items.isEmpty()) {
            auto item = items.takeLast();
            if (item->property("text").toString() == "Overview" &&
                item->metaObject()->indexOfProperty("checkable") >= 0) {
                overview = item;
                break;
            }
            items.append(item->childItems());
        }
        QVERIFY(overview);
        overview->forceActiveFocus();
        for (int page = 1; page < 7; ++page) {
            QTest::keyClick(window, Qt::Key_Tab);
            QTest::keyClick(window, Qt::Key_Space);
            QCOMPARE(root->property("page").toInt(), page);
        }
        QTest::keyClick(window, Qt::Key_Backtab);
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(root->property("page").toInt(), 5);
    }
    void consentHeaderFitsLongExecutableName() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QObject *dialog = nullptr;
        for (auto object : root->findChildren<QObject *>())
            if (object->property("title").toString().startsWith("Allow Reword in ")) {
                dialog = object;
                break;
            }
        QVERIFY(dialog);
        auto title = QString("Allow Reword in notes-") + QString(220, 'x') + ".exe?";
        QVERIFY(QQmlProperty::write(dialog, "title", title));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto header = dialog->property("header").value<QQuickItem *>();
        QVERIFY(header);
        QCOMPARE(header->property("text").toString(), title);
        auto available = header->width() - header->property("leftPadding").toReal() -
                         header->property("rightPadding").toReal();
        QVERIFY2(header->property("contentWidth").toReal() <= available + 1,
                 "A long executable name must wrap inside the permission dialog.");
        const auto captures = qEnvironmentVariable("REWORD_DIALOG_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            window->setWidth(880);
            window->setHeight(620);
            for (bool dark : {false, true}) {
                controller.set("dark", dark);
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(
                    captures + (dark ? "/dark-consent-long.png" : "/light-consent-long.png")));
            }
        }
    }
    void consentNoIsReachableWithKeyboard() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        auto editor = root->findChild<QQuickItem *>("workspaceEditor");
        QVERIFY(editor);
        editor->forceActiveFocus();
        QObject *dialog = nullptr;
        for (auto object : root->findChildren<QObject *>())
            if (object->property("title").toString().startsWith("Allow Reword in ")) {
                dialog = object;
                break;
            }
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(dialog->property("opened").toBool());
        bool reached = false;
        for (int i = 0; i < 20; ++i) {
            auto item = window->activeFocusItem();
            if (item && item->property("text").toString() == "No") {
                reached = true;
                break;
            }
            QTest::keyClick(window, Qt::Key_Tab);
        }
        QVERIFY2(reached, "The explicit No action must be reachable using Tab.");
        QSignalSpy activated(window->activeFocusItem(), SIGNAL(clicked()));
        QVERIFY(activated.isValid());
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(activated.count(), 1);
        // Another queued application may legitimately reuse the same popup next.
    }
    void consentWaitsForModalThenKeyboardNoPersists() {
        QTemporaryDir data;
        auto native = std::make_unique<UiMetadataAdapter>();
        auto adapter = native.get();
        reword::Controller controller(data.path(), nullptr, std::move(native));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QObject *pause = nullptr, *consent = nullptr;
        for (auto object : root->findChildren<QObject *>()) {
            if (object->metaObject()->indexOfMethod("open()") < 0)
                continue;
            auto title = object->property("title").toString();
            if (title == "Pause Reword")
                pause = object;
            else if (title.startsWith("Allow Reword in "))
                consent = object;
        }
        QVERIFY(pause && consent);
        QVERIFY(QMetaObject::invokeMethod(pause, "open"));
        QTRY_VERIFY(pause->property("opened").toBool());
        adapter->application("synthetic-note-app");
        QTRY_COMPARE_WITH_TIMEOUT(controller.pendingName(), QString("synthetic-note-app"), 12000);
        QVERIFY2(!consent->property("visible").toBool(),
                 "A new application must wait behind the active modal.");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!pause->property("visible").toBool());
        QTRY_VERIFY(consent->property("opened").toBool());
        bool reached = false;
        for (int i = 0; i < 20; ++i) {
            auto item = window->activeFocusItem();
            if (item && item->property("text").toString() == "No") {
                reached = true;
                break;
            }
            QTest::keyClick(window, Qt::Key_Tab);
        }
        QVERIFY(reached);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(controller.pendingName().isEmpty());
        QTRY_VERIFY(!consent->property("visible").toBool());
        auto applications = controller.apps();
        QCOMPARE(applications.size(), 1);
        QCOMPARE(applications.first().toMap()["decision"].toString(), QString("deny"));
        QVERIFY(controller.history().isEmpty());
    }
    void widgetPaletteTracksSelectedTheme() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        QMenu menu;
        for (bool dark : {false, true}) {
            controller.set("dark", dark);
            QTRY_COMPARE(menu.palette().color(QPalette::WindowText),
                         root->property("ink").value<QColor>());
        }
    }
    void savedMarkupNeverLoadsRemoteResources() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
            reword::Controller controller(data.path(), nullptr,
                                          std::make_unique<UiMetadataAdapter>());
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
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
            reword::Controller controller(data.path(), nullptr,
                                          std::make_unique<UiMetadataAdapter>());
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
        reword::Controller reopened(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QVERIFY(reopened.property("recoveryOriginal").toString().isEmpty());
        QCOMPARE(reopened.history().size(), 0);
    }
    void passageLockRejectsConcurrentWork() {
        QTemporaryDir data;
        reword::Controller first(data.path(), nullptr, std::make_unique<UiMetadataAdapter>()),
            second(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        QSignalSpy results(&controller, &reword::Controller::correctedText);
        controller.fix(QString("teh ").repeated(10000));
        controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 12000);
        QCOMPARE(results.count(), 0);
    }
    void storageFailureDoesNotEscape() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
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
