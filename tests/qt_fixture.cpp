#include <QApplication>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QSocketNotifier>
#include <QTimer>
#include <QSaveFile>
#include <QWidget>
#include <QVBoxLayout>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
class FixtureEntry final : public QLineEdit {
  public:
    bool rejectNative = false;
    int rejectedEvents = 0;
    explicit FixtureEntry(QWidget *parent) : QLineEdit(parent) {}

  protected:
    void inputMethodEvent(QInputMethodEvent *event) override {
        if (rejectNative) {
            ++rejectedEvents;
            event->accept();
            return;
        }
        QLineEdit::inputMethodEvent(event);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("reword_qt_fixture");
    QWidget window;
    window.setWindowTitle("Reword synthetic Qt fixture");
    auto layout = new QVBoxLayout(&window);
    auto entry = new FixtureEntry(&window);
    layout->addWidget(entry);
    const auto statePath = qEnvironmentVariable("REWORD_FIXTURE_STATE");
    if (!statePath.isEmpty())
        QObject::connect(entry, &QLineEdit::textChanged, &app, [statePath](const QString &text) {
            QSaveFile file(statePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(text.toUtf8());
                file.commit();
            }
        });
    window.resize(800, 600);
    if (qEnvironmentVariableIsSet("REWORD_FIXTURE_MAXIMIZE"))
        window.showMaximized();
    else
        window.show();
    entry->setFocus();
    fcntl(0, F_SETFL, O_NONBLOCK);
    QSocketNotifier input(0, QSocketNotifier::Read);
    QByteArray pending;
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char bytes[512];
        for (;;) {
            auto count = read(0, bytes, sizeof(bytes));
            if (count <= 0)
                break;
            pending.append(bytes, count);
        }
        for (;;) {
            auto end = pending.indexOf('\n');
            if (end < 0)
                break;
            auto command = pending.left(end);
            pending.remove(0, end + 1);
            if (command == "quit")
                app.quit();
            else if (command == "reject-native")
                entry->rejectNative = true;
            else if (command == "accept-native")
                entry->rejectNative = false;
            else if (command == "password")
                entry->setEchoMode(QLineEdit::Password);
            else if (command == "plain")
                entry->setEchoMode(QLineEdit::Normal);
            else if (command.startsWith("cursor:"))
                entry->setCursorPosition(command.mid(7).toInt());
            else if (command == "select-all")
                entry->selectAll();
            else if (command.startsWith("text:"))
                entry->setText(QString::fromUtf8(command.mid(5)));
            else if (command == "rejected-events") {
                std::printf("REJECTED:%d\n", entry->rejectedEvents);
                std::fflush(stdout);
            } else if (command == "snapshot") {
                auto encoded = entry->text().toUtf8().toBase64();
                std::printf("DATA:%s\n", encoded.constData());
                std::fflush(stdout);
            }
        }
    });
    QTimer::singleShot(120000, &app, &QCoreApplication::quit);
    return app.exec();
}
