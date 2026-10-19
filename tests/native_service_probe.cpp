#include "app/service.h"
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <iostream>
#include <chrono>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("Reword integration probe");
    QTemporaryDir data;
    reword::Store store(data.path());
    auto fixturePath =
        QFileInfo(QCoreApplication::applicationDirPath() + "/reword_fixture").canonicalFilePath();
    store.discover(fixturePath, "Synthetic fixture");
    store.decide(fixturePath, "allow");
    store.setValue("statistics", true);
    auto observer = reword::createAdapter();
    reword::Service service(data.path());
    bool seenFixture = false;
    QString serviceState, lastMessage;
    QObject::connect(&service, &reword::Service::application, [&](QString id, QString) {
        if (id == fixturePath)
            seenFixture = true;
    });
    QObject::connect(&service, &reword::Service::message,
                     [&](QString message) { lastMessage = message; });
    bool ready = false, shortcutHealthy = false;
    QObject::connect(&service, &reword::Service::state, [&](QString description, bool keys) {
        serviceState = description;
        shortcutHealthy = keys;
        ready = true;
    });
    service.start();
    QElapsedTimer startup;
    startup.start();
    while (!ready && startup.elapsed() < 5000) {
        app.processEvents();
        QThread::msleep(10);
    }
    if (!ready) {
        service.stop();
        std::cerr << "FAIL: service startup did not finish.\n";
        return 1;
    }
    QProcess fixture;
    auto fixtureEnvironment = QProcessEnvironment::systemEnvironment();
    fixtureEnvironment.insert("REWORD_FIXTURE_CHANGE_TIMING", "1");
    fixture.setProcessEnvironment(fixtureEnvironment);
    QList<qint64> changes;
    QByteArray changeBuffer;
    bool invalidTiming = false;
    const auto monotonicMicros = [] {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    auto collectChanges = [&] {
        changeBuffer += fixture.readAllStandardOutput();
        if (changeBuffer.size() > 4096) {
            invalidTiming = true;
            changeBuffer.clear();
            return;
        }
        while (changeBuffer.contains('\n')) {
            auto end = changeBuffer.indexOf('\n');
            auto parts = changeBuffer.left(end).split(':');
            changeBuffer.remove(0, end + 1);
            bool sequenceOk = false, timestampOk = false;
            auto sequence = parts.size() == 3 ? parts[1].toULongLong(&sequenceOk) : 0;
            auto timestamp = parts.size() == 3 ? parts[2].toLongLong(&timestampOk) : 0;
            if (parts.size() != 3 || parts[0] != "CHANGE" || !sequenceOk || !timestampOk ||
                sequence != quint64(changes.size() + 1) || timestamp <= 0 ||
                (!changes.isEmpty() && timestamp < changes.last()) || changes.size() >= 256) {
                invalidTiming = true;
                continue;
            }
            changes.append(timestamp);
        }
    };
    QObject::connect(&fixture, &QProcess::readyReadStandardOutput, collectChanges);
    auto nativeTiming = [&](qsizetype before, qint64 started) {
        collectChanges();
        return !invalidTiming && changeBuffer.isEmpty() && changes.size() >= before + 2 &&
               changes[before] >= started && changes.last() <= monotonicMicros();
    };
    fixture.start(fixturePath, {});
    if (!fixture.waitForStarted()) {
        service.stop();
        return 2;
    }
    auto pump = [&](int milliseconds) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < milliseconds) {
            app.processEvents();
            QThread::msleep(10);
        }
    };
    auto until = [&](auto predicate, int timeout = 3000) {
        QElapsedTimer t;
        t.start();
        do {
            pump(40);
            if (predicate())
                return true;
        } while (t.elapsed() < timeout);
        return false;
    };
    auto command = [&](QByteArray text) {
        fixture.write(text + '\n');
        fixture.waitForBytesWritten();
        pump(160);
    };
    auto text = [&] { return observer->inspect(true).text; };
    auto finish = [&](const char *message, int code) {
        std::cout << message << '\n';
        if (code != 0) {
            auto field = observer->inspect(false);
            std::cerr << "seen=" << seenFixture
                      << " fixtureStillFocused=" << (field.app == fixturePath && field.focused)
                      << " state=" << serviceState.toStdString()
                      << " message=" << lastMessage.toStdString() << "\n";
        }
        fixture.write("quit\n");
        fixture.waitForBytesWritten();
        fixture.waitForFinished(2000);
        service.stop();
        return code;
    };
    if (!until([&] {
            auto field = observer->inspect(false);
            return field.app == fixturePath && field.focused && field.known && !field.locked;
        }))
        return finish("FAIL: fixture focus unavailable.", 1);
    auto blocker = reword::createAdapter();
    if (!blocker->shortcuts("Ctrl+Alt+Shift+X", "Ctrl+Alt+Shift+Y", "Ctrl+Alt+Shift+Z"))
        return finish("FAIL: native conflict fixture could not reserve shortcuts.", 1);
    store.setValue("fixShortcut", "Ctrl+Alt+Shift+X");
    if (!until([&] {
            return store.value("fixShortcut").toString() == "Ctrl+Alt+R" &&
                   lastMessage.contains("restored", Qt::CaseInsensitive);
        }) ||
        !shortcutHealthy)
        return finish("FAIL: native shortcut conflict did not restore settings and registrations.",
                      1);
    blocker.reset();
    std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),
                                                               &XCloseDisplay);
    if (!display)
        return finish("FAIL: private X display could not open for shortcut delivery.", 1);
    auto control = XKeysymToKeycode(display.get(), XK_Control_L);
    auto alt = XKeysymToKeycode(display.get(), XK_Alt_L);
    auto shift = XKeysymToKeycode(display.get(), XK_Shift_L);
    auto pause = XKeysymToKeycode(display.get(), XK_p);
    if (!control || !alt || !shift || !pause)
        return finish("FAIL: private keyboard has no shortcut keycodes.", 1);
    bool delivered = true;
    for (auto [key, down] :
         {std::pair{control, true}, std::pair{alt, true}, std::pair{shift, true},
          std::pair{pause, true}, std::pair{pause, false}, std::pair{shift, false},
          std::pair{alt, false}, std::pair{control, false}})
        delivered = (XTestFakeKeyEvent(display.get(), key, down, 0) != 0) && delivered;
    XSync(display.get(), False);
    if (!delivered || !until([&] { return store.paused(); }))
        return finish("FAIL: restored native Pause shortcut did not arrive.", 1);
    store.pause(0);
    std::cout << "PASS: native X11 shortcut conflict restores settings and Pause delivery.\n";
    if (!observer->shortcuts("Ctrl+Alt+Shift+U", "Ctrl+Alt+Shift+V", "Ctrl+Alt+Shift+W"))
        return finish("FAIL: queued-shortcut fixture registration failed.", 1);
    const auto firstKey = XKeysymToKeycode(display.get(), XK_u);
    const auto secondKey = XKeysymToKeycode(display.get(), XK_v);
    for (auto key : {control, alt, shift, firstKey})
        XTestFakeKeyEvent(display.get(), key, true, 0);
    XTestFakeKeyEvent(display.get(), firstKey, false, 0);
    XTestFakeKeyEvent(display.get(), secondKey, true, 0);
    XSync(display.get(), False);
    auto isAction = [](const QString &value) {
        return value == "fix" || value == "undo" || value == "pause";
    };
    const bool firstPremature = isAction(observer->takeAction());
    pump(40);
    const bool heldPremature = isAction(observer->takeAction());
    for (auto key : {secondKey, shift, alt, control})
        XTestFakeKeyEvent(display.get(), key, false, 0);
    XSync(display.get(), False);
    const bool releasePremature = isAction(observer->takeAction());
    pump(30);
    const auto firstAction = observer->takeAction();
    pump(30);
    const auto secondAction = observer->takeAction();
    if (firstPremature || heldPremature || releasePremature ||
        firstAction != "fix" || secondAction != "undo")
        return finish("FAIL: overlapping shortcuts bypass release/grace ordering.", 1);
    std::cout << "PASS: overlapping native shortcuts wait for all releases and focus grace.\n";
    pump(300);
    command("text:");
    auto beforeFirst = changes.size();
    auto firstStarted = monotonicMicros();
    command("text:teh ");
    if (!until([&] { return text() == "the "; }))
        return finish("FAIL: first attempt was not corrected.", 1);
    if (!nativeTiming(beforeFirst, firstStarted))
        return finish("FAIL: first native edit has no valid monotonic timing records.", 1);
    command("text:");
    command("text:teh ");
    pump(300);
    if (text() != "teh ")
        return finish("FAIL: second attempt was not preserved.", 1);
    command("text:");
    auto beforeThird = changes.size();
    auto thirdStarted = monotonicMicros();
    command("text:teh ");
    if (!until([&] { return text() == "the "; }))
        return finish("FAIL: third attempt was not corrected.", 1);
    if (!nativeTiming(beforeThird, thirdStarted))
        return finish("FAIL: third native edit has no valid monotonic timing records.", 1);
    std::cout << "PASS: first/third native edits have bounded monotonic fixture timestamps.\n";
    service.action("undo");
    if (!until([&] { return text() == "teh "; }))
        return finish("FAIL: guarded live Undo failed.", 1);
    store.pause(-1);
    command("text:");
    command("text:teh ");
    pump(300);
    if (text() != "teh ")
        return finish("FAIL: pause did not suppress correction.", 1);
    store.pause(0);
    command("text:im here with teh book");
    command("select-all");
    const auto fixKey = XKeysymToKeycode(display.get(), XK_r);
    collectChanges();
    auto beforeHeldShortcut = changes.size();
    for (auto key : {control, alt, fixKey})
        XTestFakeKeyEvent(display.get(), key, true, 0);
    XSync(display.get(), False);
    pump(200);
    collectChanges();
    const bool changedWhileHeld = changes.size() != beforeHeldShortcut;
    for (auto key : {fixKey, alt, control})
        XTestFakeKeyEvent(display.get(), key, false, 0);
    XSync(display.get(), False);
    if (changedWhileHeld)
        return finish("FAIL: selection changed before the native shortcut was released.", 1);
    if (!until([&] { return text() == "I am here with the book"; }, 12000))
        return finish("FAIL: released native selection shortcut did not correct text.", 1);
    if (store.histories().size() != 1)
        return finish("FAIL: selected history was not saved.", 1);
    const auto undoKey = XKeysymToKeycode(display.get(), XK_z);
    for (auto key : {control, alt, undoKey})
        XTestFakeKeyEvent(display.get(), key, true, 0);
    XSync(display.get(), False);
    pump(200);
    for (auto key : {undoKey, alt, control})
        XTestFakeKeyEvent(display.get(), key, false, 0);
    XSync(display.get(), False);
    if (!until([&] { return text() == "im here with teh book"; }))
        return finish("FAIL: released native Undo shortcut lost the saved correction.", 1);
    command("password");
    auto password = observer->inspect(true);
    if (!password.password || !password.text.isEmpty())
        return finish("FAIL: password field protection failed.", 1);
    collectChanges();
    auto beforeProtected = changes.size();
    service.action("fix");
    pump(100);
    collectChanges();
    if (invalidTiming || !changeBuffer.isEmpty() || changes.size() != beforeProtected)
        return finish("FAIL: protected operation produced text-change timing records.", 1);
    if (store.histories().size() != 1)
        return finish("FAIL: protected operation was recorded.", 1);
    return finish("PASS: live first/second/third, live Undo, pause, selected "
                  "correction/history/Undo, password exclusion.",
                  0);
}
