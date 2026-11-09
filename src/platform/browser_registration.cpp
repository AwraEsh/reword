#include "browser_registration.h"
#include "browser_extension_origin.h"
#include "core/private_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <stdexcept>
#include <unistd.h>

namespace {
constexpr auto HostDescription = "Reword offline browser correction";
constexpr qint64 MaxManifestBytes = 8192;
constexpr auto SharedWrite = QFileDevice::WriteGroup | QFileDevice::WriteOther;

void requireOwnedDirectory(const QString &path) {
    reword::validateStorageLocation(path);
    const QFileInfo directory(path);
    if (!directory.isDir() || directory.ownerId() != geteuid() ||
        (directory.permissions() & SharedWrite))
        throw std::runtime_error("Use an existing browser data directory owned by your account.");
}
}

void reword::registerBrowserHost(const QString &profile, const QString &executable, bool remove) {
    requireOwnedDirectory(profile);
    const auto directory = QDir(profile).absoluteFilePath("NativeMessagingHosts");
    validateStorageLocation(directory);
    if (!QFileInfo::exists(directory)) {
        if (remove)
            return;
        if (!QDir().mkdir(directory) && !QFileInfo(directory).isDir())
            throw std::runtime_error("Could not create the browser's NativeMessagingHosts directory.");
    }
    requireOwnedDirectory(directory);

    const auto path = QDir(directory).filePath(QString::fromLatin1(BrowserHostName) + ".json");
    validateStorageLocation(path);
    const auto lockPath = path + ".lock";
    validateStorageLocation(lockPath);
    QLockFile lock(lockPath);
    if (!lock.tryLock(0))
        throw std::runtime_error("Another registration is in progress. Try again.");

    const QJsonObject identity{{"name", BrowserHostName}, {"description", HostDescription},
        {"type", "stdio"}, {"allowed_origins", QJsonArray{BrowserExtensionOrigin}}};
    QString previousExecutable;
    if (QFileInfo::exists(path)) {
        validateExistingStorageFile(path);
        QFile existing(path);
        if ((QFileInfo(path).permissions() & SharedWrite) || !existing.open(QIODevice::ReadOnly))
            throw std::runtime_error("The existing browser registration cannot be read safely.");
        const auto bytes = existing.read(MaxManifestBytes + 1);
        auto previous = bytes.size() <= MaxManifestBytes ? QJsonDocument::fromJson(bytes).object() :
                                                         QJsonObject{};
        previousExecutable = previous.take("path").toString();
        if (previous != identity || !QDir::isAbsolutePath(previousExecutable))
            throw std::runtime_error("The existing browser registration does not belong to Reword.");
        if (remove) {
            if (!existing.remove())
                throw std::runtime_error("Could not remove the browser registration.");
            return;
        }
    } else if (remove) {
        return;
    }

    const QFileInfo binary(executable);
    const auto canonical = binary.canonicalFilePath();
    if (canonical.isEmpty() || !binary.isFile() || !binary.isExecutable())
        throw std::runtime_error("The Reword browser host is not executable.");
    if (previousExecutable == canonical)
        return;
    auto manifest = identity;
    manifest.insert("path", canonical);
    const auto bytes = QJsonDocument(manifest).toJson();
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly) ||
        !destination.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        destination.write(bytes) != bytes.size() || !destination.commit())
        throw std::runtime_error("Could not save the browser registration.");
}
