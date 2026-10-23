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
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QSemaphore>
#include <QThreadPool>
#include <QtConcurrent>
#include <QFileInfo>
#include <QTextLayout>
#include <QGlyphRun>
#include <QFontDatabase>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <utility>
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
// Own only synthetic field state; all Controller/Service/Store/Engine paths stay real.
class UiSelectionAdapter final : public reword::Adapter {
    QMutex mutex_;
    reword::Field field_;
    QString action_;
  public:
    std::atomic_int actions{0}, writes{0};
    explicit UiSelectionAdapter(QString text) {
        field_.app = "synthetic-ui-selection";
        field_.id = "selection";
        field_.known = field_.editable = field_.focused = field_.plain = true;
        field_.locked = false;
        field_.text = std::move(text);
        field_.caret = field_.selectionEnd = field_.text.size();
        field_.selectionStart = 0;
    }
    void requestCorrection() {
        QMutexLocker lock(&mutex_);
        action_ = "fix";
    }
    reword::Field inspect(bool text) override {
        QMutexLocker lock(&mutex_);
        auto field = field_;
        if (!text)
            field.text.clear();
        return field;
    }
    reword::Field readExpected(const reword::Field &expected) override {
        QMutexLocker lock(&mutex_);
        return expected.app == field_.app && expected.id == field_.id ? field_ : reword::Field{};
    }
    bool changed() override { return false; }
    QString takeAction() override {
        QMutexLocker lock(&mutex_);
        auto action = std::exchange(action_, {});
        if (!action.isEmpty())
            ++actions;
        return action;
    }
    bool replace(const reword::Field &expected, const QString &text, int caret) override {
        QMutexLocker lock(&mutex_);
        if (!reword::sameTarget(expected, field_))
            return false;
        ++writes;
        field_.text = text;
        field_.caret = caret;
        field_.selectionStart = field_.selectionEnd = -1;
        ++field_.revision;
        return true;
    }
    bool shortcuts(const QString &, const QString &, const QString &) override { return true; }
    QString description() const override { return "Owned selection fixture"; }
};
// Release even after a failed assertion, before Controller joins its Service worker.
struct UiBlockedWorkerPool {
    QThreadPool *pool = QThreadPool::globalInstance();
    int previousMaximum = pool->maxThreadCount();
    QSemaphore started, release;
    QFuture<void> blocker;
    bool released = false;
    UiBlockedWorkerPool() {
        pool->waitForDone();
        pool->setMaxThreadCount(1);
        blocker = QtConcurrent::run([this] { started.release(); release.acquire(); });
    }
    bool ready() { return started.tryAcquire(1, 2000); }
    void unblock() {
        if (!released) {
            released = true;
            release.release();
            blocker.waitForFinished();
        }
    }
    ~UiBlockedWorkerPool() {
        unblock();
        pool->waitForDone();
        pool->setMaxThreadCount(previousMaximum);
    }
};
class SettingsReadController final : public reword::Controller {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ measuredSettings NOTIFY updated)
  public:
    mutable int settingsReads = 0;
    explicit SettingsReadController(const QString &directory)
        : reword::Controller(directory, nullptr, std::make_unique<UiMetadataAdapter>()) {}
    QVariantMap measuredSettings() const {
        ++settingsReads;
        return reword::Controller::settings();
    }
};
class HistoryReadController final : public reword::Controller {
    Q_OBJECT
  public:
    mutable int historyReads = 0;
    explicit HistoryReadController(const QString &directory)
        : reword::Controller(directory, nullptr, std::make_unique<UiMetadataAdapter>()) {}
    QVariantList history() const override {
        ++historyReads;
        return reword::Controller::history();
    }
};
class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void hiddenHistoryDoesNotReloadOnEveryCorrection() {
        QTemporaryDir data;
        HistoryReadController controller(data.path());
        controller.recordApplied("teh", "the");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        QTest::qWait(200);
        controller.historyReads = 0;
        emit controller.updated();
        QTest::qWait(50);
        QCOMPARE(controller.historyReads, 0);
        root->setProperty("page", 4);
        QTRY_VERIFY(controller.historyReads > 0);
        QTRY_COMPARE(root->property("historySnapshot").toList().size(), 1);
        controller.recordApplied("recieve", "receive");
        QTRY_COMPARE(root->property("historySnapshot").toList().size(), 2);
        QSignalSpy updates(&controller, &reword::Controller::updated);
        controller.historyReads = 0;
        emit controller.updated();
        QTest::qWait(50);
        QVERIFY2(controller.historyReads <= 2 * updates.count(),
                 qPrintable(QString("%1 history reads for %2 updates")
                                .arg(controller.historyReads).arg(updates.count())));
        root->setProperty("page", 0);
        controller.historyReads = 0;
        emit controller.updated();
        QTest::qWait(50);
        QCOMPARE(controller.historyReads, 0);
    }
    void nativeSelectionExposesBusyAndCancel_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode : {"apply", "cancel", "unchanged"})
            QTest::newRow(mode) << QString(mode);
    }
    void nativeSelectionExposesBusyAndCancel() {
        QFETCH(QString, mode);
        QTemporaryDir data;
        reword::Store store(data.path());
        store.discover("synthetic-ui-selection", "Owned selection fixture");
        store.decide("synthetic-ui-selection", "allow");
        const QString original = mode == "unchanged" ? "hello" : "im here";
        auto adapter = new UiSelectionAdapter(original);
        reword::Controller controller(data.path(), nullptr,
                                      std::unique_ptr<reword::Adapter>(adapter));
        UiBlockedWorkerPool workers;
        QVERIFY(workers.ready());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto cancel = engine.rootObjects().first()->findChild<QQuickItem *>("cancelCorrection");
        QVERIFY(cancel);
        QVERIFY(!controller.busy());
        adapter->requestCorrection();
        QTRY_COMPARE(adapter->actions.load(), 1);
        QTRY_VERIFY(QFileInfo::exists(data.path() + "/operation.lock"));
        // Missing native lifecycle delivery leaves the real Cancel control hidden.
        QTRY_VERIFY_WITH_TIMEOUT(controller.busy(), 2000);
        QTRY_VERIFY(cancel->isVisible());
        controller.fix("im elsewhere");
        QVERIFY(controller.notice().contains("already running"));
        QVERIFY(controller.busy());
        if (mode == "cancel") {
            QVERIFY(QMetaObject::invokeMethod(cancel, "clicked", Qt::DirectConnection));
            QTest::qWait(50);
            QVERIFY(controller.busy()); // Worker has not acknowledged cancellation yet.
        }
        workers.unblock();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 12000);
        QTRY_VERIFY(!cancel->isVisible());
        QCOMPARE(adapter->inspect(true).text, mode == "apply" ? QString("I am here") : original);
        QCOMPARE(adapter->writes.load(), mode == "apply" ? 1 : 0);
        QCOMPARE(controller.history().size(), mode == "apply" ? 1 : 0);
    }
    void fcitxSelectionExposesBusyAndCancel() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr,
                                      std::make_unique<UiMetadataAdapter>());
        reword::RecoveryClient addon;
        QSignalSpy cancelled(&addon, &reword::RecoveryClient::cancelRequested);
        addon.start(data.path());
        QTRY_VERIFY(addon.ready());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto cancel = engine.rootObjects().first()->findChild<QQuickItem *>("cancelCorrection");
        QVERIFY(cancel);
        QVERIFY(!controller.busy());

        QVERIFY(addon.beginOperation());
        QTRY_VERIFY(controller.busy());
        QTRY_VERIFY(cancel->isVisible());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked", Qt::DirectConnection));
        QTRY_COMPARE(cancelled.count(), 1);
        QVERIFY(controller.busy()); // Cancellation remains cooperative.
        QVERIFY(cancel->isVisible());
        QVERIFY(addon.endOperation());
        QTRY_VERIFY(!controller.busy());
        QTRY_VERIFY(!cancel->isVisible());
        controller.pause(0);
        QTRY_VERIFY(addon.ready());
        QVERIFY(addon.beginOperation());
        QTRY_VERIFY(controller.busy());
        controller.pause(-1);
        QTRY_COMPARE(cancelled.count(), 2);
        QVERIFY(controller.busy());
        QVERIFY(addon.endOperation());
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.history().isEmpty());
    }
    void oneUpdateSharesSettingsAcrossBindings() {
        QTemporaryDir data;
        SettingsReadController controller(data.path());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QTest::qWait(300);
        QVERIFY(controller.settingsReads > 0);
        controller.settingsReads = 0;
        emit controller.updated();
        QTest::qWait(100);
        QVERIFY2(controller.settingsReads <= 2,
                 qPrintable(QString("One backend update read settings %1 times")
                                .arg(controller.settingsReads)));
        QObject *english = nullptr;
        auto root = engine.rootObjects().first();
        for (auto row : root->findChildren<QObject *>())
            if (row->property("settingKey").toString() == "english")
                for (auto control : row->children())
                    if (control->property("checked").isValid())
                        english = control;
        QVERIFY(english);
        QVERIFY(english->property("checked").toBool());
        controller.set("english", false);
        QTRY_VERIFY(!english->property("checked").toBool());
        controller.set("english", true);
        QTRY_VERIFY(english->property("checked").toBool());
    }
    void populatedListsKeepActionsInsideTheViewport_data() {
        QTest::addColumn<QString>("surface");
        for (auto surface : {"applications", "personal", "replacements", "insights"})
            QTest::newRow(surface) << QString(surface);
    }
    void populatedListsKeepActionsInsideTheViewport() {
        QFETCH(QString, surface);
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        reword::Store fixture(data.path());
        const QString longWord(64, 'm');
        const QString longName(200, 'm');
        fixture.discover("/synthetic/reword-ui/" + longName, longName);
        fixture.decide("/synthetic/reword-ui/" + longName, "allow");
        fixture.discover("/synthetic/reword-ui/notes", "Notes");
        fixture.decide("/synthetic/reword-ui/notes", "deny");
        fixture.discover("/synthetic/reword-ui/reader", "Reader");
        fixture.decide("/synthetic/reword-ui/reader", "later");
        controller.addWord(longWord);
        controller.addWord(QString::fromUtf8("واژه‌آزمایشی"));
        controller.replacement(longWord, QString(128, 'W'));
        controller.set("statistics", true);
        auto options = fixture.options();
        fixture.count(longWord, QString(64, 'W'), options);
        fixture.count(QString::fromUtf8("ازمایشی"), QString::fromUtf8("آزمایشی"), options);
        emit controller.updated();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->setWidth(880);
        window->setHeight(620);
        root->setProperty("page", surface == "applications" ? 3 : surface == "insights" ? 5 : 2);
        QStringList failures;
        for (bool dark : {false, true}) {
            controller.set("dark", dark);
            QTest::qWait(150);
            QQuickItem *action = nullptr;
            auto items = window->contentItem()->childItems();
            while (!items.isEmpty()) {
                auto item = items.takeLast();
                const auto text = item->property("text").toString();
                const bool button = item->property("checkable").isValid();
                if (item->isVisible() &&
                    ((surface == "applications" && item->property("displayText").toString() == "Allowed") ||
                     (button && surface == "personal" && text == longWord + "  ×") ||
                     (button && surface == "replacements" && text == "Remove") ||
                     (button && surface == "insights" && text == "Practice"))) {
                    action = item;
                    break;
                }
                items.append(item->childItems());
            }
            QVERIFY(action);
            for (auto parent = action->parentItem(); parent; parent = parent->parentItem()) {
                if (!parent->property("contentY").isValid())
                    continue;
                const auto anchor = surface == "replacements" ? action->parentItem() : action;
                const auto offset = anchor->mapToItem(parent, QPointF()).y();
                const auto maximum = std::max(0.0, parent->property("contentHeight").toDouble() -
                                                       parent->height());
                parent->setProperty("contentY", std::clamp(
                    parent->property("contentY").toDouble() + offset - 40.0, 0.0, maximum));
                break;
            }
            QTest::qWait(100);
            const auto captureDirectory = qEnvironmentVariable("REWORD_UI_CAPTURE_DIR");
            if (!captureDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(captureDirectory));
                QVERIFY(window->grabWindow().save(captureDirectory + "/populated-" + surface +
                                                  (dark ? "-dark.png" : "-light.png")));
            }
            const auto bounds = action->mapRectToScene(action->boundingRect());
            QList<QRectF> textBounds;
            items = window->contentItem()->childItems();
            while (!items.isEmpty()) {
                auto item = items.takeLast();
                const auto text = item->property("text").toString();
                const bool expectedText =
                    (surface == "applications" && text == longName) ||
                    (surface == "personal" && text == longWord + "  ×") ||
                    (surface == "replacements" && text == longWord + " → " + QString(128, 'W')) ||
                    (surface == "insights" && (text == longWord || text == QString(64, 'W')));
                if (item->isVisible() && expectedText &&
                    !item->property("checkable").isValid() &&
                    item->property("contentWidth").isValid()) {
                    const auto inkWidth = item->property("truncated").toBool()
                                              ? item->width()
                                              : item->property("contentWidth").toDouble();
                    const auto ink = item->mapRectToScene(QRectF(0, 0, inkWidth, item->height()));
                    const auto rightLimit = surface == "personal" ? bounds.right() : bounds.left();
                    if (ink.right() > rightLimit + 1.0)
                        failures << QString("%1 text overlaps or exceeds its action: right=%2 limit=%3")
                                        .arg(dark ? "dark" : "light")
                                        .arg(ink.right()).arg(rightLimit);
                    textBounds << ink;
                }
                items.append(item->childItems());
            }
            QVERIFY(!textBounds.isEmpty());
            for (int i = 0; i < textBounds.size(); ++i)
                for (int j = i + 1; j < textBounds.size(); ++j)
                    if (textBounds[i].intersected(textBounds[j]).width() > 1.0 &&
                        textBounds[i].intersected(textBounds[j]).height() > 1.0)
                        failures << "Populated word labels overlap each other";
            if (bounds.left() < 214 || bounds.right() > window->width() ||
                bounds.top() < 122 || bounds.bottom() > window->height())
                failures << QString("%1 action outside viewport: x=%2 y=%3 width=%4 height=%5")
                                .arg(dark ? "dark" : "light")
                                .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height());
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join("; ")));
    }
    void permissionIndicatorsStayVisibleInBothThemes() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        reword::Store fixture(data.path());
        fixture.discover("/synthetic/reword-ui/notes", "Notes");
        fixture.decide("/synthetic/reword-ui/notes", "allow");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->setWidth(880);
        window->setHeight(620);
        root->setProperty("page", 3);
        const auto luminance = [](const QColor &color) {
            const auto linear = [](double value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
                   0.0722 * linear(color.blueF());
        };
        QStringList failures;
        for (bool dark : {false, true}) {
            controller.set("dark", dark);
            QTest::qWait(150);
            int checked = 0;
            auto items = window->contentItem()->childItems();
            while (!items.isEmpty()) {
                auto item = items.takeLast();
                items.append(item->childItems());
                if (!item->isVisible() || !item->property("displayText").isValid())
                    continue;
                auto indicator = item->property("indicator").value<QQuickItem *>();
                auto background = item->property("background").value<QQuickItem *>();
                if (!indicator || !background)
                    continue;
                const auto foreground = indicator->property("color").value<QColor>();
                const auto surface = background->property("color").value<QColor>();
                QVERIFY(foreground.isValid() && surface.isValid());
                const auto first = luminance(foreground), second = luminance(surface);
                const auto contrast = (std::max(first, second) + 0.05) /
                                      (std::min(first, second) + 0.05);
                if (contrast < 3.0)
                    failures << QString("%1 indicator contrast=%2")
                                    .arg(dark ? "dark" : "light").arg(contrast);
                ++checked;
            }
            QVERIFY(checked >= 2); // Appearance and the actual application permission.
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join("; ")));
    }
    void practiceDialogFitsLongestAcceptedWord() {
        QTemporaryDir data;
        reword::Controller controller(data.path(), nullptr, std::make_unique<UiMetadataAdapter>());
        reword::Store fixture(data.path());
        controller.set("statistics", true);
        const QString answer(64, 'W');
        fixture.count("misspelling", answer, fixture.options());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        engine.load(QUrl::fromLocalFile(QString(REWORD_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto root = engine.rootObjects().first();
        auto window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->setWidth(880);
        window->setHeight(620);
        root->setProperty("page", 5);
        QTRY_VERIFY(controller.platform().startsWith("Controlled UI metadata fixture"));
        QTest::qWait(150);
        QQuickItem *practice = nullptr;
        auto items = window->contentItem()->childItems();
        while (!items.isEmpty()) {
            auto item = items.takeLast();
            if (item->isVisible() && item->property("text").toString() == "Practice" &&
                item->property("checkable").isValid()) {
                practice = item;
                break;
            }
            items.append(item->childItems());
        }
        QVERIFY(practice);
        QPointer<QQuickItem> practiceGuard(practice);
        QSignalSpy clicked(practice, SIGNAL(clicked()));
        practice->forceActiveFocus();
        QCOMPARE(window->activeFocusItem(), practice);
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY2(practiceGuard, "The Practice delegate disappeared during activation.");
        QCOMPARE(clicked.count(), 1);
        QObject *dialog = nullptr;
        for (auto object : root->findChildren<QObject *>())
            if (object->property("title").toString() == "Practice spelling" &&
                object->property("answer").isValid() && object->property("opened").isValid()) {
                dialog = object;
                break;
            }
        QVERIFY(dialog);
        QTRY_VERIFY2(dialog->property("opened").toBool(),
                     qPrintable(QString("visible=%1 answerCharacters=%2 width=%3")
                                    .arg(dialog->property("visible").toBool())
                                    .arg(dialog->property("answer").toString().size())
                                    .arg(dialog->property("width").toDouble())));
        QCOMPARE(dialog->property("answer").toString(), answer);
        QStringList failures;
        for (bool dark : {false, true}) {
            controller.set("dark", dark);
            QTest::qWait(100);
            const auto captures = qEnvironmentVariable("REWORD_DIALOG_CAPTURE_DIR");
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                QVERIFY(window->grabWindow().save(captures +
                         (dark ? "/dark-practice-long.png" : "/light-practice-long.png")));
            }
            bool foundQuestion = false, foundClose = false;
            auto visibleItems = window->contentItem()->childItems();
            while (!visibleItems.isEmpty()) {
                auto item = visibleItems.takeLast();
                visibleItems.append(item->childItems());
                if (!item->isVisible())
                    continue;
                const auto text = item->property("text").toString();
                const bool question = text == "Type: " + answer &&
                                      item->property("contentWidth").isValid();
                const bool close = text == "Close" && item->property("checkable").isValid();
                if (!question && !close)
                    continue;
                foundQuestion |= question;
                foundClose |= close;
                const auto width = question ? item->property("contentWidth").toDouble()
                                            : item->width();
                const auto bounds = item->mapRectToScene(QRectF(0, 0, width, item->height()));
                if (bounds.left() < 0 || bounds.top() < 0 || bounds.right() > window->width() ||
                    bounds.bottom() > window->height())
                    failures << QString("%1 exceeds viewport: x=%2 y=%3 width=%4 height=%5")
                                    .arg(question ? "Practice question" : "Close action")
                                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height());
            }
            QVERIFY(foundQuestion && foundClose);
            const auto x = dialog->property("x").toDouble();
            const auto y = dialog->property("y").toDouble();
            const auto width = dialog->property("width").toDouble();
            const auto height = dialog->property("height").toDouble();
            if (x < 0 || y < 0 || x + width > window->width() || y + height > window->height())
                failures << QString("Practice dialog exceeds viewport: x=%1 y=%2 width=%3 height=%4")
                                .arg(x).arg(y).arg(width).arg(height);
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join("; ")));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("opened").toBool());
    }
    void renderingFontsCoverBothLanguages() {
        QVERIFY2(!QFontDatabase::families().isEmpty(),
                 "The offscreen renderer must discover installed font families.");
#ifdef Q_OS_WIN
        const QFont font("Segoe UI");
#else
        const QFont font("Noto Sans");
#endif
        QTextLayout layout(QString::fromUtf8("Reword یک متن فارسی"), font);
        layout.beginLayout();
        auto line = layout.createLine();
        QVERIFY(line.isValid());
        line.setLineWidth(1000);
        layout.endLayout();
        const auto runs = layout.glyphRuns();
        QVERIFY2(!runs.isEmpty(), "The UI renderer must load fonts for its review captures.");
        for (const auto &run : runs) {
            QVERIFY2(run.rawFont().isValid(),
                     "Box-font fallback is not valid rendered UI evidence.");
            for (auto glyph : run.glyphIndexes())
                QVERIFY2(glyph != 0,
                         "English/Persian review text must not render as missing glyphs.");
        }
    }
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
            QList<QQuickItem *> items{content->parentItem()};
            while (!items.isEmpty()) {
                auto item = items.takeLast();
                items.append(item->childItems());
                if ((!item->inherits("QQuickText") && !item->inherits("QQuickTextInput") &&
                     !item->inherits("QQuickTextEdit")) ||
                    item->property("text").toString().isEmpty() || !item->isVisible())
                    continue;
                auto textColor = item->property("color").value<QColor>();
                if (!textColor.isValid())
                    continue;
                auto backdrop = surface;
                for (auto ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
                    auto backing = ancestor->property("background").value<QQuickItem *>();
                    auto color = backing ? backing->property("color").value<QColor>() : QColor();
                    if (color.isValid() && color.alphaF() >= 0.99) {
                        backdrop = color;
                        break;
                    }
                }
                auto a = luminance(textColor), b = luminance(backdrop);
                auto textContrast = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
                QVERIFY2(textContrast >= 4.5,
                         qPrintable(QString("Dialog text contrast %1 (%2, %3 theme)")
                                        .arg(textContrast)
                                        .arg(item->metaObject()->className())
                                        .arg(dark ? "dark" : "light")));
            }
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
