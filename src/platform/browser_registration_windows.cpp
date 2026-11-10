#include "browser_registration.h"
#include "browser_manifest.h"
#include "core/private_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <windows.h>

namespace {
using RegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)>;
constexpr auto RegistryAccess = KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_32KEY;

std::wstring registryPath(const QString &browser) {
    const auto root = browser == "chrome" ? "Software\\Google\\Chrome" : "Software\\Chromium";
    return (QString::fromLatin1(root) + "\\NativeMessagingHosts\\" +
            QLatin1StringView(reword::BrowserHostName)).toStdWString();
}

RegistryKey openRegistry(const std::wstring &path) {
    HKEY handle = nullptr;
    const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, RegistryAccess, &handle);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Could not open the current user's browser registration.");
    return RegistryKey(handle, RegCloseKey);
}

std::optional<QString> registeredManifest(HKEY key) {
    if (!key)
        return std::nullopt;
    wchar_t buffer[32768]{};
    DWORD type = 0;
    DWORD bytes = sizeof buffer;
    const auto status = RegQueryValueExW(key, nullptr, nullptr, &type,
                                        reinterpret_cast<BYTE *>(buffer), &bytes);
    if (status == ERROR_FILE_NOT_FOUND)
        return std::nullopt;
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof buffer ||
        bytes % sizeof(wchar_t) || buffer[bytes / sizeof(wchar_t) - 1] != L'\0')
        throw std::runtime_error("The existing browser registration cannot be read safely.");
    const auto path = QString::fromWCharArray(buffer, bytes / sizeof(wchar_t) - 1);
    if (path.contains(QChar::Null))
        throw std::runtime_error("The existing browser registration contains an invalid path.");
    return QDir::fromNativeSeparators(path);
}

QString readOwnedManifest(const QString &path, const QJsonObject &identity) {
    if (!QFileInfo::exists(path))
        return {};
    reword::validateExistingStorageFile(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("The existing Reword browser manifest could not be read.");
    const auto bytes = file.read(reword::MaxBrowserManifestBytes + 1);
    auto existing = bytes.size() <= reword::MaxBrowserManifestBytes
        ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
    const auto executable = existing.take("path").toString();
    if (existing != identity || !QDir::isAbsolutePath(executable))
        throw std::runtime_error("The existing browser manifest does not belong to Reword.");
    return executable;
}

void writeRegistry(RegistryKey &key, const std::wstring &subkey, const QString &path) {
    if (!key) {
        HKEY handle = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, RegistryAccess,
                            nullptr, &handle, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Could not create the current user's browser registration.");
        key.reset(handle);
    }
    // Recheck before assigning the unnamed value. Another installer may
    // have registered a different host while the manifest was being written.
    if (const auto previous = registeredManifest(key.get()); previous && *previous != path)
        throw std::runtime_error("The existing browser registration does not belong to Reword.");
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    if (RegSetValueExW(key.get(), nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(native.c_str()),
                       DWORD((native.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        throw std::runtime_error("Could not save the current user's browser registration.");
}
}

void reword::registerWindowsBrowserHost(const QString &browser, const QString &dataDirectory,
                                        const QString &executable, bool remove) {
    if (browser != "chrome" && browser != "chromium")
        throw std::runtime_error("Choose Chrome or Chromium for browser registration.");
    if (!QDir::isAbsolutePath(dataDirectory))
        throw std::runtime_error("Browser registration requires an absolute Reword data directory.");
    const auto directory = QDir(dataDirectory).absoluteFilePath("browser-hosts");
    const auto path = QDir(directory).filePath(browser + ".json");
    const auto lockPath = path + ".lock";
    validateStorageLocation(lockPath);
    validateStorageLocation(path);

    // An interrupted installation can leave a registry value pointing to a
    // missing manifest. Lock the same directory when repairing or removing it.
    const auto subkey = registryPath(browser);
    if (remove && !QFileInfo::exists(directory) && !registeredManifest(openRegistry(subkey).get()))
        return;
    if (!QDir().mkpath(directory))
        throw std::runtime_error("Could not create the private browser registration directory.");
    restrictStoragePath(directory, true);
    QLockFile lock(lockPath);
    if (!lock.tryLock(0))
        throw std::runtime_error("Another registration is in progress. Try again.");
    auto key = openRegistry(subkey);
    const auto previous = registeredManifest(key.get());
    if (previous && *previous != path)
        throw std::runtime_error("The existing browser registration does not belong to Reword.");

    auto identity = browserManifestIdentity();
    const auto oldExecutable = readOwnedManifest(path, identity);
    if (remove) {
        if (!executable.isEmpty()) {
            const auto installed = QDir::toNativeSeparators(QFileInfo(executable).canonicalFilePath());
            if (installed.isEmpty() || oldExecutable != installed)
                return;
        }
        if (const auto latest = registeredManifest(key.get()); latest) {
            if (*latest != path)
                throw std::runtime_error("The existing browser registration does not belong to Reword.");
            const auto status = RegDeleteValueW(key.get(), nullptr);
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
                throw std::runtime_error("Could not remove the current user's browser registration.");
        }
        if (!oldExecutable.isEmpty() && !QFile::remove(path))
            throw std::runtime_error("Could not remove the Reword browser manifest.");
        return;
    }

    const QFileInfo binary(executable);
    const auto canonical = QDir::toNativeSeparators(binary.canonicalFilePath());
    if (canonical.isEmpty() || !binary.isFile() ||
        !binary.fileName().endsWith(".exe", Qt::CaseInsensitive))
        throw std::runtime_error("Use an existing Reword browser host executable.");
    if (oldExecutable != canonical) {
        identity.insert("path", canonical);
        const auto bytes = QJsonDocument(identity).toJson();
        if (bytes.size() > MaxBrowserManifestBytes)
            throw std::runtime_error("The browser host executable path is too long.");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("Could not save the Reword browser manifest.");
    }
    if (!previous)
        writeRegistry(key, subkey, path);
}
