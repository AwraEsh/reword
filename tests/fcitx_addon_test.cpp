#include <QtTest>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <getopt.h>
#include "../src/platform/fcitx_addon.cpp"
#include <fcitx/inputcontextmanager.h>

namespace {
class OwnedLockService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.ScreenSaver")
  public:
    bool active = false;
  public Q_SLOTS:
    bool GetActive() const { return active; }
};

class OwnedContext final : public fcitx::InputContext {
  public:
    explicit OwnedContext(fcitx::InputContextManager &manager)
        : fcitx::InputContext(manager, "reword-synthetic-fixture") { created(); }
    ~OwnedContext() override { destroy(); }
    const char *frontend() const override { return "reword-owned-test"; }
  protected:
    void commitStringImpl(const std::string &) override {}
    void deleteSurroundingTextImpl(int, unsigned) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};

class FcitxAddonTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;
    OwnedLockService lock_;
  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("FCITX_CONFIG_HOME", config_.path().toUtf8());
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.gnome.ScreenSaver"));
        QVERIFY(bus.registerObject("/org/gnome/ScreenSaver", &lock_, QDBusConnection::ExportAllSlots));
    }
    void unsafeObservationReleasesOwnedJobText_data() {
        QTest::addColumn<QString>("boundary");
        for (const auto &name : {"password", "sensitive", "no-surrounding", "denied", "paused", "focus"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void unsafeObservationReleasesOwnedJobText() {
        QFETCH(QString, boundary);
        QTemporaryDir data;
        QVERIFY(data.isValid());
        qputenv("REWORD_DATA_DIR", data.path().toUtf8());
        qputenv("XDG_SESSION_TYPE", "wayland");
        reword::RecoveryServer recovery;
        QVERIFY(recovery.listen(data.path()));
        char executable[] = "reword-owned-fcitx-test";
        char disable[] = "--disable";
        char all[] = "all";
        char *arguments[] = {executable, disable, all, nullptr};
        // GNU getopt keeps its parser position across owned instances.
        optind = 0;
        fcitx::Instance instance(3, arguments);
        // Instance construction installs event handlers, but initialize()
        // registers the inputState factory those handlers require. Disable all
        // addons so this fixture never connects a real desktop frontend.
        instance.initialize();
        RewordAddon addon(&instance);
        QTRY_VERIFY(addon.recovery_->active());
        OwnedContext context(instance.inputContextManager());
        context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
        context.surroundingText().setText("teh", 3, 0);
        context.focusIn();
        const QString id = "fcitx:reword-synthetic-fixture";
        addon.store_->discover(id, "Synthetic fixture");
        addon.store_->decide(id, "allow");
        QVERIFY(addon.permitted(&context, id));
        QVERIFY(addon.unlocked());

        // These are owner-held copies of a pending selection operation. The
        // future is ready but not consumed by the Fcitx loop during this event.
        addon.job_.context = context.watch();
        addon.job_.app = id;
        addon.job_.before = addon.job_.original = "synthetic original";
        addon.job_.after = addon.job_.corrected = "synthetic result";
        addon.job_.valid = true;
        addon.pending_ = addon.job_;
        addon.prefix_ = "synthetic prefix";
        addon.observedHash_ = "synthetic hash";
        addon.context_ = addon.observedContext_ = "synthetic context";
        addon.cancel_.store(false);
        std::promise<reword::Change> result;
        addon.future_ = result.get_future();
        result.set_value({});

        if (boundary == "password")
            context.setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::SurroundingText) | fcitx::CapabilityFlag::Password);
        else if (boundary == "sensitive")
            context.setCapabilityFlags(fcitx::CapabilityFlags(fcitx::CapabilityFlag::SurroundingText) | fcitx::CapabilityFlag::Sensitive);
        else if (boundary == "no-surrounding")
            context.setCapabilityFlags({});
        else if (boundary == "denied")
            addon.store_->decide(id, "deny");
        else if (boundary == "paused")
            addon.store_->pause(-1);
        else
            context.focusOut();
        QVERIFY(!addon.permitted(&context, id));
        addon.observeRetry(&context);
        QVERIFY(addon.cancel_.load());
        QVERIFY(!addon.pending_.valid);
        QVERIFY(addon.prefix_.isEmpty());
        const bool retainsText = !addon.job_.before.isEmpty() || !addon.job_.after.isEmpty() ||
                                 !addon.job_.original.isEmpty() || !addon.job_.corrected.isEmpty();
        QVERIFY2(!retainsText, "Unsafe observation retained owner-held selection text.");
    }
    void timerCancelsPendingJobAfterPolicyChange_data() {
        QTest::addColumn<QString>("boundary");
        for (const auto &name : {"paused", "denied", "locked"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void timerCancelsPendingJobAfterPolicyChange() {
        QFETCH(QString, boundary);
        QTemporaryDir data;
        QVERIFY(data.isValid());
        qputenv("REWORD_DATA_DIR", data.path().toUtf8());
        qputenv("XDG_SESSION_TYPE", "wayland");
        reword::RecoveryServer recovery;
        QVERIFY(recovery.listen(data.path()));
        char executable[] = "reword-owned-fcitx-test";
        char disable[] = "--disable", all[] = "all";
        char *arguments[] = {executable, disable, all, nullptr};
        optind = 0;
        fcitx::Instance instance(3, arguments);
        instance.initialize();
        RewordAddon addon(&instance);
        QTRY_VERIFY(addon.recovery_->active());
        OwnedContext context(instance.inputContextManager());
        context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
        context.surroundingText().setText("teh", 3, 0);
        context.focusIn();
        const QString id = "fcitx:reword-synthetic-fixture";
        addon.store_->discover(id, "Synthetic fixture");
        addon.store_->decide(id, "allow");
        QVERIFY(addon.permitted(&context, id));
        QVERIFY(addon.unlocked());
        addon.job_.context = context.watch();
        addon.job_.app = id;
        addon.job_.before = addon.job_.original = "synthetic original";
        addon.job_.after = addon.job_.corrected = "synthetic result";
        addon.job_.valid = true;
        addon.cancel_.store(false);
        std::promise<reword::Change> result;
        addon.future_ = result.get_future();
        const auto finish = qScopeGuard([&] { result.set_value({}); lock_.active = false; });
        // No pending Undo, key, focus or surrounding-text event accompanies a
        // Settings/lock change while the worker is still outstanding.
        QVERIFY(!addon.pending_.valid);
        if (boundary == "paused")
            addon.store_->pause(-1);
        else if (boundary == "denied")
            addon.store_->decide(id, "deny");
        else
            lock_.active = true;
        auto deadline = instance.eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 160000, 0,
            [&](fcitx::EventSourceTime *, uint64_t) { instance.eventLoop().exit(); return false; });
        instance.eventLoop().exec();
        QVERIFY(addon.future_.valid());
        QVERIFY(addon.cancel_.load());
        QVERIFY(addon.job_.original.isEmpty());
        QVERIFY(addon.job_.before.isEmpty());
        QVERIFY(addon.job_.corrected.isEmpty());
        QVERIFY(addon.job_.after.isEmpty());
    }
};
} // namespace

QTEST_GUILESS_MAIN(FcitxAddonTest)
#include "fcitx_addon_test.moc"
