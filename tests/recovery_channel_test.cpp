#include <QtTest>
#include <memory>
#include <vector>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtEndian>
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
