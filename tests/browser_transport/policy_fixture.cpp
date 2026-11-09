#include "core/store.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <sys/stat.h>

int main(int argc, char **argv) {
    umask(0077);
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 5)
        return 2;
    const auto application = args[2];
    const auto key = args[3];
    const auto value = args[4];
    try {
        reword::Store store(args[1]);
        if (key == "rule" && value == "remove") {
            store.removeApplication(application);
        } else if (key == "rule" && (value == "allow" || value == "shortcut" || value == "deny")) {
            store.discover(application, QFileInfo(application).fileName());
            store.decide(application, value);
        } else if ((key == "english" || key == "persian" || key == "browsers") &&
                   (value == "true" || value == "false")) {
            store.setCorrectionOption(key, value == "true");
        } else if (key == "undoShortcut" && (value == "Ctrl+Alt+Z" || value == "Ctrl+Alt+K")) {
            store.setCorrectionOption(key, value);
        } else if (key == "pauseUntil" && (value == "-1" || value == "0")) {
            store.pause(value.toInt());
        } else {
            return 2;
        }
    } catch (...) {
        return 1;
    }
    return 0;
}
