#include <QtTest>
#include <memory>
#include <vector>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtEndian>
#include <QProcess>
#include <QScopeGuard>
#ifndef _WIN32
#include <grp.h>
#include <unistd.h>
#endif
#include "platform/recovery_channel.h"
using namespace reword;
class RecoveryChannelTest : public QObject {
    Q_OBJECT
    static void hello(QLocalSocket &socket, const QString &directory) {
        socket.connectToServer(recoveryEndpoint(directory));
        QTRY_COMPARE(socket.state(), QLocalSocket::ConnectedState);
        socket.write("RWH1");
        QTRY_COMPARE(socket.bytesAvailable(), qint64(4));
        QCOMPARE(socket.readAll(), QByteArray("RWR1"));
    }
  private slots:
    void cancellationPrecedesCoalescedDelivery() {
        QTemporaryDir data;
        QLocalServer server;
        QVERIFY(server.listen(recoveryEndpoint(data.path())));
        RecoveryClient client;
        client.start(data.path());
        QTRY_VERIFY(server.hasPendingConnections());
        auto peer = server.nextPendingConnection();
        QTRY_COMPARE(peer->bytesAvailable(), qint64(4));
        QCOMPARE(peer->readAll(), QByteArray("RWH1"));
        peer->write("RWR1");
        QTRY_VERIFY(client.ready());
        QVERIFY(client.beginOperation());
        QVERIFY(client.submit("original", "result"));
        QStringList order;
        connect(&client, &RecoveryClient::cancelRequested, this, [&] { order << "cancel"; });
        connect(&client, &RecoveryClient::delivered, this, [&] {
            order << "delivered";
            client.endOperation();
        });
        peer->write("OKAYCNCL");
        QTRY_COMPARE(order.size(), 2);
        QCOMPARE(order, QStringList({"cancel", "delivered"}));
    }
    void crossAccountServerNeverReceivesGreeting() {
#ifdef _WIN32
        QSKIP("The isolated foreign-UID server probe is Linux-only.");
#else
        if (::geteuid() != 0)
            QSKIP("Run only in the disposable Linux CI runner with root for UID separation.");
        QTemporaryDir data;
        const auto endpoint = QDir::tempPath() + '/' + recoveryEndpoint(data.path());
        const auto cleanup = qScopeGuard([&] { QFile::remove(endpoint); });
        QProcess impostor;
        impostor.setChildProcessModifier([] {
            if (::setgroups(0, nullptr) || ::setgid(65534) || ::setuid(65534))
                ::_exit(100);
        });
        const QString script = R"PY(
import os, socket, sys
s = socket.socket(socket.AF_UNIX)
s.bind(sys.argv[1])
os.chmod(sys.argv[1], 0o666)
s.listen(1)
s.settimeout(3)
print('READY', flush=True)
try:
    c, _ = s.accept()
    c.settimeout(1)
    data = c.recv(16)  # Require EOF; timeout must fail instead of masquerading as rejection.
    print('GREETING' if data else 'EMPTY', flush=True)
    c.close()
finally:
    s.close()
    os.unlink(sys.argv[1])
)PY";
        impostor.start("/usr/bin/python3", {"-c", script, endpoint});
        QVERIFY(impostor.waitForStarted());
        QByteArray output;
        QTRY_VERIFY((output += impostor.readAllStandardOutput()).contains("READY\n"));
        RecoveryClient client;
        QSignalSpy activity(&client, &RecoveryClient::activeChanged);
        client.start(data.path());
        QTRY_COMPARE(impostor.state(), QProcess::NotRunning);
        output += impostor.readAllStandardOutput();
        QCOMPARE(impostor.exitCode(), 0);
        QCOMPARE(output, QByteArray("READY\nEMPTY\n"));
        QVERIFY(!client.active());
        QVERIFY(!client.submit("must never leave the account", "result"));
        QCOMPARE(activity.count(), 0);
#endif
    }
    void wirePreservesExactUtf16AndEnforcesBounds() {
        QString original = QString::fromUtf8("متن 🙂 ");
        original += QChar(0xd800); // Preserve even unpaired units; never normalize an original.
        auto frame = encodeRecovery(original, "corrected");
        QVERIFY(!frame.isEmpty());
        QString a, b;
        QVERIFY(decodeRecovery(frame, a, b));
        QCOMPARE(a, original);
        QCOMPARE(b, QString("corrected"));
        auto edge = QString::fromUcs4(U"🙂").repeated(50000);
        auto other = QString::fromUcs4(U"🙃").repeated(50000);
        QCOMPARE(encodeRecovery(edge, other).size(), qsizetype(MaxRecoveryFrame));
        QVERIFY(encodeRecovery(edge + "x", other).isEmpty());
        QVERIFY(encodeRecovery("x", QString(50001, 'a')).isEmpty());
        QVERIFY(encodeRecovery("same", "same").isEmpty());
        QVERIFY(encodeRecovery({}, "x").isEmpty());
        QVERIFY(!decodeRecovery(frame.chopped(1), a, b));
        QVERIFY(!decodeRecovery(frame + 'x', a, b));
        auto huge = QByteArray("RWP1") + QByteArray(8, char(0xff));
        QVERIFY(!decodeRecovery(huge, a, b));
        QCOMPARE(a, original);
    }
    void confirmedDeliveryAndDisconnectGate() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        RecoveryClient client;
        QSignalSpy received(&server, &RecoveryServer::passage);
        QSignalSpy delivered(&client, &RecoveryClient::delivered);
        client.start(data.path());
        QTRY_VERIFY(client.active());
        QVERIFY(client.submit("original", "result"));
        QTRY_COMPARE(received.count(), 1);
        QTRY_COMPARE(delivered.count(), 1);
        QCOMPARE(received.first().at(0).toString(), QString("original"));
        QTest::qWait(1100); // A healthy idle lifecycle channel must remain open.
        QVERIFY(client.active());
        server.stop();
        QTRY_VERIFY(!client.active());
        QVERIFY(!client.submit("late", "result"));
        QCOMPARE(received.count(), 1);
    }
    void operationLifecycleIsAggregatedAndCancellableWithoutText() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        RecoveryClient first, second;
        QSignalSpy activity(&server, &RecoveryServer::operationActive);
        QSignalSpy firstCancel(&first, &RecoveryClient::cancelRequested);
        QSignalSpy secondCancel(&second, &RecoveryClient::cancelRequested);
        QSignalSpy passages(&server, &RecoveryServer::passage);
        first.start(data.path());
        second.start(data.path());
        QTRY_VERIFY(first.ready());
        QTRY_VERIFY(second.ready());

        QVERIFY(first.beginOperation());
        QTRY_COMPARE(activity.count(), 1);
        QCOMPARE(activity.at(0).at(0).toBool(), true);
        QVERIFY(second.beginOperation());
        QTest::qWait(20);
        QCOMPARE(activity.count(), 1); // Aggregate state stays active.
        server.cancelOperations();
        QTRY_COMPARE(firstCancel.count(), 1);
        QTRY_COMPARE(secondCancel.count(), 1);
        QCOMPARE(passages.count(), 0);

        QVERIFY(first.endOperation());
        QTest::qWait(20);
        QCOMPARE(activity.count(), 1); // The second peer still owns work.
        second.stop();
        QTRY_COMPARE(activity.count(), 2);
        QCOMPARE(activity.at(1).at(0).toBool(), false);
        QCOMPARE(passages.count(), 0);
    }
    void coalescedLifecycleFramesAreAtomicAndDisconnectClearsBusy() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        QSignalSpy activity(&server, &RecoveryServer::operationActive);
        QSignalSpy passages(&server, &RecoveryServer::passage);
        QLocalSocket socket;
        hello(socket, data.path());
        socket.write("RWB1RWE1RWB1");
        QTRY_COMPARE(activity.count(), 3);
        QCOMPARE(activity.at(0).at(0).toBool(), true);
        QCOMPARE(activity.at(1).at(0).toBool(), false);
        QCOMPARE(activity.at(2).at(0).toBool(), true);
        QCOMPARE(passages.count(), 0);
        socket.abort();
        QTRY_COMPARE(activity.count(), 4);
        QCOMPARE(activity.at(3).at(0).toBool(), false);
    }
    void localDiagnosticsReturnOnlyDeliveryCounter() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        QLocalSocket socket;
        hello(socket, data.path());
        socket.write("RWS1");
        QTRY_COMPARE(socket.bytesAvailable(), qint64(12));
        auto status = socket.readAll();
        QCOMPARE(status.left(4), QByteArray("STAT"));
        QCOMPARE(qFromLittleEndian<quint64>(status.constData() + 4), quint64(0));
        socket.write(encodeRecovery("original", "result"));
        QTRY_COMPARE(socket.bytesAvailable(), qint64(4));
        QCOMPARE(socket.readAll(), QByteArray("OKAY"));
        socket.write("RWS1");
        QTRY_COMPARE(socket.bytesAvailable(), qint64(12));
        status = socket.readAll();
        QCOMPARE(qFromLittleEndian<quint64>(status.constData() + 4), quint64(1));
    }
    void partialAndExtraFramesAreAtomic() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        QSignalSpy received(&server, &RecoveryServer::passage);
        QLocalSocket socket;
        hello(socket, data.path());
        const auto frame = encodeRecovery("original", "result");
        socket.write(frame.left(14));
        QTest::qWait(50);
        QCOMPARE(received.count(), 0);
        socket.write(frame.mid(14));
        QTRY_COMPARE(received.count(), 1);
        QTRY_COMPARE(socket.bytesAvailable(), qint64(4));
        QCOMPARE(socket.readAll(), QByteArray("OKAY"));
        socket.write(frame + 'x');
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(received.count(), 1);
    }
    void invalidAndOversizedFramesNeverDeliver() {
        for (auto frame : {QByteArray("NOPE") + QByteArray(8, '\0'),
                           QByteArray("RWP1") + QByteArray(8, char(0xff)),
                           encodeRecovery("original", "result").chopped(1)}) {
            QTemporaryDir data;
            RecoveryServer server;
            QVERIFY(server.listen(data.path()));
            QSignalSpy received(&server, &RecoveryServer::passage);
            QLocalSocket socket;
            hello(socket, data.path());
            socket.write(frame);
            QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::UnconnectedState, 2500);
            QCOMPARE(received.count(), 0);
        }
    }
    void incompleteHelloTimesOutAndClientsAreLimited() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        QLocalSocket partial;
        partial.connectToServer(recoveryEndpoint(data.path()));
        QTRY_COMPARE(partial.state(), QLocalSocket::ConnectedState);
        partial.write("RW");
        QTRY_COMPARE_WITH_TIMEOUT(partial.state(), QLocalSocket::UnconnectedState, 2500);
        QTRY_COMPARE(server.clientCount(), 0);
        std::vector<std::unique_ptr<QLocalSocket>> sockets;
        for (int i = 0; i < 4; ++i) {
            auto socket = std::make_unique<QLocalSocket>();
            hello(*socket, data.path());
            sockets.push_back(std::move(socket));
        }
        QCOMPARE(server.clientCount(), 4);
        QLocalSocket overflow;
        overflow.connectToServer(recoveryEndpoint(data.path()));
        QTest::qWait(100);
        QTRY_COMPARE(overflow.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(server.clientCount(), 4);
#ifndef _WIN32
        const auto permissions = QFileInfo(server.fullServerName()).permissions();
        QVERIFY(!(permissions &
                  (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
#endif
    }
    void clientKeepsOnlyLatestPendingPassage() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        RecoveryClient client;
        QSignalSpy received(&server, &RecoveryServer::passage);
        connect(&server, &RecoveryServer::passage, &client, [&](QString original, QString) {
            if (original == "first") {
                QVERIFY(client.submit("second", "result"));
                QVERIFY(client.submit("latest", "result"));
            }
        });
        client.start(data.path());
        QTRY_VERIFY(client.active());
        QVERIFY(client.submit("first", "result"));
        QTRY_COMPARE(received.count(), 2);
        QCOMPARE(received.at(0).at(0).toString(), QString("first"));
        QCOMPARE(received.at(1).at(0).toString(), QString("latest"));
    }
    void deliveryCallbackCannotSendAnObsoletePendingPassage() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        RecoveryClient client;
        QSignalSpy received(&server, &RecoveryServer::passage);
        connect(&server, &RecoveryServer::passage, &client, [&](QString original, QString) {
            if (original == "first")
                QVERIFY(client.submit("obsolete", "result"));
        });
        bool firstAck = true;
        connect(&client, &RecoveryClient::delivered, &client, [&] {
            if (std::exchange(firstAck, false))
                QVERIFY(client.submit("fresh", "result"));
        });
        client.start(data.path());
        QTRY_VERIFY(client.active());
        QVERIFY(client.submit("first", "result"));
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 2500);
        QCOMPARE(received.last().at(0).toString(), QString("fresh"));
        QTest::qWait(100);
        QCOMPARE(received.count(), 2);
    }
    void destructionDoesNotCallBackIntoItsOwner() {
        QTemporaryDir data;
        RecoveryServer server;
        QVERIFY(server.listen(data.path()));
        auto client = std::make_unique<RecoveryClient>();
        client->start(data.path());
        QTRY_VERIFY(client->active());
        int callbacks = 0;
        connect(client.get(), &RecoveryClient::activeChanged, this, [&](bool active) {
            if (!active)
                ++callbacks;
        });
        connect(client.get(), &RecoveryClient::deliveryFailed, this, [&] { ++callbacks; });
        QVERIFY(client->submit("confirmed original", "result"));
        client.reset();
        QCOMPARE(callbacks, 0);
    }
    void lifecycleCallbacksMayDestroyClient() {
        for (bool onDelivery : {false, true}) {
            QTemporaryDir data;
            RecoveryServer server;
            QVERIFY(server.listen(data.path()));
            QPointer<RecoveryClient> client = new RecoveryClient;
            client->start(data.path());
            QTRY_VERIFY(client->active());
            if (onDelivery) {
                connect(client, &RecoveryClient::delivered, this, [&] { delete client.data(); });
                QVERIFY(client->submit("original", "result"));
                QVERIFY(client->submit("pending", "result"));
            } else {
                connect(client, &RecoveryClient::activeChanged, this, [&](bool active) {
                    if (!active)
                        delete client.data();
                });
                QVERIFY(client->submit("original", "result"));
                server.stop();
            }
            QTRY_VERIFY(client.isNull());
        }
    }
    void absentServerNeverAuthorizesOrStoresPassages() {
        QTemporaryDir data;
        RecoveryClient client;
        client.start(data.path());
        QTest::qWait(50);
        QVERIFY(!client.active());
        QVERIFY(!client.submit("original", "result"));
        client.stop();
    }
};
QTEST_GUILESS_MAIN(RecoveryChannelTest)
#include "recovery_channel_test.moc"
