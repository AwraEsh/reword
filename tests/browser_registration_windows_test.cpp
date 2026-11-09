#include "platform/browser_registration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace {
constexpr wchar_t chromeKey[] = L"Software\\Google\\Chrome\\NativeMessagingHosts\\org.reword.browser";
constexpr wchar_t chromiumKey[] = L"Software\\Chromium\\NativeMessagingHosts\\org.reword.browser";

class PrivateRegistry {
    const std::wstring key_ = ("Software\\RewordBrowserTest-" +
        QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString();
    HKEY root_ = nullptr;
    bool mapped_ = false;

  public:
    bool map() {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                            nullptr, &root_, nullptr) != ERROR_SUCCESS)
            return false;
        mapped_ = RegOverridePredefKey(HKEY_CURRENT_USER, root_) == ERROR_SUCCESS;
        return mapped_;
    }

    ~PrivateRegistry() {
        if (mapped_ && RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) != ERROR_SUCCESS)
            return;
        if (root_) {
            RegCloseKey(root_);
            RegDeleteTreeW(HKEY_CURRENT_USER, key_.c_str());
        }
    }
};

QString registryValue(const wchar_t *key, const wchar_t *name, REGSAM view = 0) {
    wchar_t value[4096]{};
    DWORD bytes = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ | view, nullptr, value,
                     &bytes) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(value);
}

LSTATUS setRegistryValue(const wchar_t *key, const wchar_t *name, const QString &value) {
    HKEY handle = nullptr;
    const auto opened = RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE,
                                        nullptr, &handle, nullptr);
    if (opened != ERROR_SUCCESS)
        return opened;
    const auto text = value.toStdWString();
    const auto result = RegSetValueExW(handle, name, 0, REG_SZ,
        reinterpret_cast<const BYTE *>(text.c_str()), DWORD((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    return result;
}

QJsonObject manifest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}

class BrowserRegistrationWindowsTest : public QObject {
    Q_OBJECT
  private slots:
    void perUserRegistrationSurvivesUpgradeAndRemovesOnlyItsEntry() {
        PrivateRegistry registry;
        QVERIFY(registry.map());
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const auto first = QCoreApplication::applicationFilePath();
        const auto next = data.filePath(QStringLiteral("Reword جدید.exe"));
        QVERIFY(QFile::copy(first, next));
        QCOMPARE(setRegistryValue(chromeKey, L"keep", "existing setting"), LSTATUS(ERROR_SUCCESS));

        reword::registerWindowsBrowserHost("chrome", data.path(), first, false);
        const auto chromeManifest = registryValue(chromeKey, nullptr, RRF_SUBKEY_WOW6432KEY);
        QVERIFY(!chromeManifest.isEmpty());
        QCOMPARE(registryValue(chromeKey, nullptr, RRF_SUBKEY_WOW6464KEY), chromeManifest);
        const auto initial = manifest(chromeManifest);
        QCOMPARE(initial.value("name").toString(), QString("org.reword.browser"));
        QCOMPARE(initial.value("type").toString(), QString("stdio"));
        QCOMPARE(initial.value("allowed_origins").toArray(),
                 QJsonArray{"chrome-extension://ehbepfpelbccljnbicecfceoaaepnamb/"});
        QCOMPARE(initial.value("path").toString(), QDir::toNativeSeparators(QFileInfo(first).canonicalFilePath()));

        reword::registerWindowsBrowserHost("chromium", data.path(), first, false);
        const auto chromiumManifest = registryValue(chromiumKey, nullptr);
        QVERIFY(!chromiumManifest.isEmpty() && chromiumManifest != chromeManifest);
        reword::registerWindowsBrowserHost("chrome", data.path(), next, false);
        reword::registerWindowsBrowserHost("chrome", data.path(), next, false);
        QCOMPARE(registryValue(chromeKey, nullptr), chromeManifest);
        QCOMPARE(manifest(chromeManifest).value("path").toString(),
                 QDir::toNativeSeparators(QFileInfo(next).canonicalFilePath()));
        QCOMPARE(manifest(chromiumManifest), initial);
        QVERIFY_EXCEPTION_THROWN(reword::registerWindowsBrowserHost("chrome", data.path(),
                                                                    data.filePath("missing.exe"), false),
                                 std::runtime_error);
        QCOMPARE(manifest(chromeManifest).value("path").toString(),
                 QDir::toNativeSeparators(QFileInfo(next).canonicalFilePath()));

        QVERIFY(QFile::remove(chromiumManifest));
        reword::registerWindowsBrowserHost("chromium", data.path(), first, false);
        QCOMPARE(manifest(chromiumManifest), initial);

        reword::registerWindowsBrowserHost("chrome", data.path(), {}, true);
        reword::registerWindowsBrowserHost("chrome", data.path(), {}, true);
        QVERIFY(registryValue(chromeKey, nullptr).isEmpty());
        QCOMPARE(registryValue(chromeKey, L"keep"), QString("existing setting"));
        QVERIFY(!QFile::exists(chromeManifest));
        QVERIFY(QFile::exists(chromiumManifest));
    }

    void preservesForeignRegistryAndManifests() {
        PrivateRegistry registry;
        QVERIFY(registry.map());
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const auto executable = QCoreApplication::applicationFilePath();
        const auto foreignPath = data.filePath("foreign.json");
        QCOMPARE(setRegistryValue(chromeKey, nullptr, foreignPath), LSTATUS(ERROR_SUCCESS));
        QVERIFY_EXCEPTION_THROWN(reword::registerWindowsBrowserHost("chrome", data.path(), executable, false),
                                 std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(reword::registerWindowsBrowserHost("chrome", data.path(), {}, true),
                                 std::runtime_error);
        QCOMPARE(registryValue(chromeKey, nullptr), foreignPath);

        reword::registerWindowsBrowserHost("chromium", data.path(), executable, false);
        const auto ownedPath = registryValue(chromiumKey, nullptr);
        QFile foreign(ownedPath);
        QVERIFY(foreign.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(foreign.write("{}"), 2);
        foreign.close();
        QVERIFY_EXCEPTION_THROWN(reword::registerWindowsBrowserHost("chromium", data.path(), executable, false),
                                 std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(reword::registerWindowsBrowserHost("chromium", data.path(), {}, true),
                                 std::runtime_error);
        QCOMPARE(registryValue(chromiumKey, nullptr), ownedPath);
        QVERIFY(foreign.open(QIODevice::ReadOnly));
        QCOMPARE(foreign.readAll(), QByteArray("{}"));
    }
};

QTEST_GUILESS_MAIN(BrowserRegistrationWindowsTest)
#include "browser_registration_windows_test.moc"
