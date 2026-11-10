#include "platform/adapter.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto adapter = reword::createAdapter();
    QProcess fixture;
    fixture.start(QCoreApplication::applicationDirPath() + "/reword_fixture", {});
    if (!fixture.waitForStarted())
        return 2;
    QElapsedTimer clock;
    clock.start();
    reword::Field f;
    while (clock.elapsed() < 8000) {
        f = adapter->inspect(false);
        if (f.app.endsWith("/reword_fixture") && f.focused)
            break;
        QThread::msleep(50);
    }
    auto fail = [&](const char *message) {
        std::cerr << message << "\n";
        fixture.kill();
        fixture.waitForFinished();
        return 1;
    };
    if (!f.app.endsWith("/reword_fixture"))
        return fail("Fixture focus was not observed.");
    if (!f.text.isEmpty())
        return fail("Metadata inspection read text.");
    if (adapter->inputMethodTarget().window)
        return fail("The multiline input method accepted a single-line entry.");
    auto otherTarget = f;
    otherTarget.id += "-different-target";
    if (!adapter->readExpected(otherTarget).text.isEmpty())
        return fail("Expected-target read accepted a different field.");
    otherTarget = f;
    otherTarget.app += "-different-application";
    if (!adapter->readExpected(otherTarget).text.isEmpty())
        return fail("Expected-target read accepted a different application.");
    f = adapter->readExpected(f);
    std::cerr << "metadata: known=" << f.known << " locked=" << f.locked << " plain=" << f.plain
              << " focused=" << f.focused << " editable=" << f.editable
              << " chars=" << f.text.size() << " caret=" << f.caret << "\n";
    if (!f.known || f.locked || !f.plain || f.text != "teh ")
        return fail("Fixture is not supported by this desktop accessibility connection.");
    auto stale = f;
    stale.text = "stale";
    const auto allow = [] { return true; };
    const auto staleEdit = adapter->replace(stale, "WRONG", 5, allow);
    if (staleEdit || staleEdit.rejection != reword::WriteRejection::TargetChanged)
        return fail("Stale transaction was not rejected at target validation.");
    bool asked = false;
    const auto deniedEdit = adapter->replace(f, "WRONG", 5, [&] { asked = true; return false; });
    if (deniedEdit || deniedEdit.rejection != reword::WriteRejection::AuthorizationDenied ||
        !asked || !reword::sameTarget(f, adapter->readExpected(f)))
        return fail("Native authorization denial did not preserve the field.");
    const auto lateEdit = adapter->replace(f, "WRONG", 5, [] {
        QThread::msleep(2 * reword::WriteAuthorizationBudgetMs);
        return true;
    });
    if (lateEdit || lateEdit.rejection != reword::WriteRejection::AuthorizationExpired ||
        !reword::sameTarget(f, adapter->readExpected(f)))
        return fail("Late authorization did not preserve the field.");
    int policyChecks = 0;
    if (adapter->replace(f, "WRONG", 5, [&] { return ++policyChecks == 1; }) ||
        policyChecks != 2 || !reword::sameTarget(f, adapter->readExpected(f)))
        return fail("Final authorization denial did not preserve the field.");
    auto fixtureSnapshot = [&](QByteArray command, QByteArray expected) {
        fixture.write(command + "\nsnapshot\n");
        return fixture.waitForReadyRead(300) && fixture.readLine().trimmed() == expected;
    };
    bool newerTextObserved = false;
    if (adapter->replace(f, "WRONG", 5, [&] {
            newerTextObserved = fixtureSnapshot("text:newer", "DATA:bmV3ZXI=");
            return newerTextObserved;
        }) || !newerTextObserved)
        return fail("A change during authorization did not stop the edit.");
    f = adapter->readExpected(f);
    if (f.text != "newer")
        return fail("A change during authorization was overwritten.");
    const auto caret = f.caret;
    bool selectionObserved = false;
    if (adapter->replace(f, "WRONG", 5, [&] {
            selectionObserved = fixtureSnapshot("select-all", "DATA:bmV3ZXI=");
            return selectionObserved;
        }) || !selectionObserved)
        return fail("A selection change during authorization did not stop the edit.");
    f = adapter->readExpected(f);
    if (f.text != "newer" || f.caret != caret || f.selectionStart != 0 ||
        f.selectionEnd != f.text.size())
        return fail("A selection-only change was not preserved.");
    bool providerArmed = false;
    const auto replacedWithSlowProvider = adapter->replace(f, "the ", 4, [&] {
        if (providerArmed)
            return true;
        auto request = QDBusMessage::createMethodCall(
            "org.gnome.ScreenSaver", "/org/gnome/ScreenSaver", "org.gnome.ScreenSaver",
            "DelayNextReply");
        auto reply = QDBusConnection::sessionBus().call(request, QDBus::Block, 100);
        providerArmed = reply.type() == QDBusMessage::ReplyMessage;
        return providerArmed;
    });
    if (!providerArmed)
        return fail("The controlled lock provider did not acknowledge the delay.");
    if (!replacedWithSlowProvider || replacedWithSlowProvider.after.id != f.id ||
        replacedWithSlowProvider.after.text != "the " || replacedWithSlowProvider.after.caret != 4)
        return fail("Guarded replacement failed with a responsive but slow lock provider.");
    f = adapter->readExpected(f);
    bool passwordObserved = false;
    if (adapter->replace(f, "WRONG", 5, [&] {
            passwordObserved = fixtureSnapshot("password", "DATA:dGhlIA==");
            return passwordObserved;
        }) || !passwordObserved || !fixtureSnapshot("", "DATA:dGhlIA=="))
        return fail("A password transition during authorization did not stop the edit.");
    f = adapter->inspect(true);
    if (!f.password || !f.text.isEmpty())
        return fail("Password field was read or unrecognized.");
    fixture.write("quit\n");
    fixture.waitForBytesWritten();
    fixture.waitForFinished(2000);
    std::cout << "PASS: GTK rejects stale targets and denied, late or changed-target authorization; "
                 "replacement with a delayed lock provider and password exclusion verified.\n";
}
