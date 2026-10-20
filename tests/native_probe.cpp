#include "platform/adapter.h"
#include <QCoreApplication>
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
    if (adapter->replace(stale, "WRONG", 5))
        return fail("Stale transaction was accepted.");
    if (!adapter->replace(f, "the ", 4))
        return fail("Guarded replacement was not confirmed.");
    fixture.write("password\n");
    fixture.waitForBytesWritten();
    QThread::msleep(150);
    f = adapter->inspect(true);
    if (!f.password || !f.text.isEmpty())
        return fail("Password field was read or unrecognized.");
    fixture.write("quit\n");
    fixture.waitForBytesWritten();
    fixture.waitForFinished(2000);
    std::cout << "PASS: metadata-only inspection, stale rejection, GTK entry replacement, password "
                 "exclusion.\n";
}
