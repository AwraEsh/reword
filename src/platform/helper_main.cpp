#include "wire.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QFile>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>

namespace {
bool writeFrame(const QJsonObject &frame) {
    auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
    qsizetype done = 0;
    while (done < bytes.size()) {
        auto count = write(STDOUT_FILENO, bytes.constData() + done, bytes.size() - done);
        if (count <= 0)
            return false;
        done += count;
    }
    return true;
}

bool requestAuthorization() {
    // Only the parent can authorize. Its single-byte response is separate
    // from the JSON command stream and only valid for this pending edit.
    pollfd response{STDIN_FILENO, POLLIN, 0};
    char reply = 0;
    if (!writeFrame({{"authorize", true}}) ||
        poll(&response, 1, reword::NativeRequestTimeoutMs) != 1 || !(response.revents & POLLIN) ||
        read(STDIN_FILENO, &reply, 1) != 1 || (reply != '0' && reply != '1'))
        throw std::runtime_error("Native authorization channel closed.");
    return reply == '1';
}
}

int main(int argc, char **argv) {
    umask(0077);
    QCoreApplication app(argc, argv);
    app.setApplicationName("Reword Native Helper");
    auto adapter = reword::createNativeAdapter();
    QByteArray pending;
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char buffer[8192];
        for (;;) {
            auto n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n == 0) {
                app.quit();
                return;
            }
            if (n < 0)
                break;
            pending.append(buffer, n);
            if (pending.size() > 1024 * 1024) {
                app.exit(2);
                return;
            }
        }
        for (;;) {
            auto end = pending.indexOf('\n');
            if (end < 0)
                break;
            auto frame = pending.left(end);
            pending.remove(0, end + 1);
            QJsonParseError parse;
            auto json = QJsonDocument::fromJson(frame, &parse);
            if (parse.error != QJsonParseError::NoError || !json.isObject()) {
                app.exit(2);
                return;
            }
            auto o = json.object();
            auto operation = o["operation"].toString();
            QJsonObject result;
            try {
                if (operation == "inspect")
                    result = {{"field", reword::encodeField(adapter->inspect(false))}};
                else if (operation == "read") {
                    reword::Field expected;
                    expected.app = o["app"].toString();
                    expected.id = o["id"].toString();
                    result = {{"field", reword::encodeField(adapter->readExpected(expected))}};
                } else if (operation == "changed")
                    result = {{"changed", adapter->changed()}};
                else if (operation == "replace-authorized") {
                    auto text = o["text"].toString();
                    int caret = o["caret"].toInt(-1);
                    bool bounded = text.size() <= 200000 && text.toUcs4().size() <= 100000 &&
                                   caret >= 0 && caret <= text.size();
                    result = {{"ok", bounded && pending.isEmpty() && adapter->replace(
                                                    reword::decodeField(o["expected"].toObject()),
                                                    text, caret, requestAuthorization)}};
                } else if (operation == "shortcuts")
                    result = {{"ok", adapter->shortcuts(o["fix"].toString(), o["undo"].toString(),
                                                        o["pause"].toString())}};
                else if (operation == "description")
                    result = {{"description", adapter->description()}};
                else if (operation == "action")
                    result = {{"action", adapter->takeAction()}};
                else {
                    app.exit(2);
                    return;
                }
            } catch (...) {
                app.exit(3);
                return;
            }
            if (!writeFrame(result)) {
                app.quit();
                return;
            }
        }
    });
    return app.exec();
}
