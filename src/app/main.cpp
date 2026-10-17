#include "controller.h"
#include "platform/recovery_channel.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QLockFile>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QLocale>
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <iostream>
#ifndef _WIN32
#include <sys/stat.h>
#endif
int main(int argc, char **argv) {
#ifndef _WIN32
    umask(0077);
#endif
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication app(argc, argv);
    app.setLayoutDirection(Qt::LeftToRight);
    app.setOrganizationName("Reword");
    app.setApplicationName("Reword");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Basic");
    QString directory = qEnvironmentVariable("REWORD_DATA_DIR");
    if (directory.isEmpty())
        directory =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reword";
    QDir().mkpath(directory);
    QLockFile lock(directory + "/instance.lock");
    lock.setStaleLockTime(0);
    auto endpoint =
        "reword-" + QString::fromLatin1(
                        QCryptographicHash::hash(QFileInfo(directory).absoluteFilePath().toUtf8(),
                                                 QCryptographicHash::Sha256)
                            .toHex()
                            .left(24));
    if (!lock.tryLock(100)) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 2000) {
            QLocalSocket activation;
            activation.connectToServer(endpoint);
            if (activation.waitForConnected(200)) {
                activation.write("show\n");
                activation.waitForBytesWritten(200);
                activation.disconnectFromServer();
                return 0;
            }
        }
        std::cerr << "Reword is already running. Open it from the tray.\n";
        return 1;
    }
    QLocalServer activationServer;
    activationServer.setMaxPendingConnections(4);
    activationServer.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(endpoint);
    if (!activationServer.listen(endpoint)) {
        std::cerr << "Reword could not create its local activation endpoint.\n";
        return 3;
    }
    QLocalServer::removeServer(reword::recoveryEndpoint(directory));
    try {
        reword::Controller controller(directory);
        QObject::connect(&activationServer, &QLocalServer::newConnection, &app, [&] {
            while (auto socket = activationServer.nextPendingConnection()) {
                socket->setReadBufferSize(32);
                auto activate = [&, socket] {
                    auto message =
                        socket->property("activationFrame").toByteArray() + socket->readAll();
                    if (message.size() > 16) {
                        socket->abort();
                        return;
                    }
                    if (!message.contains('\n')) {
                        socket->setProperty("activationFrame", message);
                        return;
                    }
                    if (message == "show\n")
                        emit controller.showWindow();
                    socket->disconnectFromServer();
                };
                QTimer::singleShot(1000, socket, [socket] { socket->abort(); });
                QObject::connect(socket, &QLocalSocket::readyRead, &app, activate);
                QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                 &QObject::deleteLater);
                if (socket->bytesAvailable() > 0)
                    activate();
            }
        });
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Backend", &controller);
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
            [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
        engine.load(QUrl("qrc:/Reword/qml/Main.qml"));
        if (!qEnvironmentVariable("REWORD_CAPTURE").isEmpty())
            QTimer::singleShot(1500, &app, [&] {
                if (!engine.rootObjects().isEmpty()) {
                    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                    if (window) {
                        window->setProperty("page",
                                            qEnvironmentVariableIntValue("REWORD_CAPTURE_PAGE"));
                        QTimer::singleShot(500, window, [window] {
                            window->grabWindow().save(qEnvironmentVariable("REWORD_CAPTURE"));
                        });
                    }
                }
            });
        if (app.arguments().contains("--smoke-test"))
            QTimer::singleShot(3000, &app, &QCoreApplication::quit);
        return app.exec();
    } catch (...) {
        std::cerr << "Reword could not open its local data. Check directory access.\n";
        return 3;
    }
}
