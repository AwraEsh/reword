#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "core/store.h"
#include <QtTest>
#include <QSqlDatabase>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
static QString securityError(const QString &path, bool directory) {
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto status = GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())),
                                        SE_FILE_OBJECT,
                                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                        &owner, nullptr, &acl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS)
        return "Could not inspect the synthetic path's security descriptor.";
    std::unique_ptr<void, decltype(&LocalFree)> descriptorOwner(descriptor, &LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !(control & SE_DACL_PROTECTED))
        return "Storage DACL still inherits permissions from its parent.";
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
        return "Could not inspect the current account token.";
    std::unique_ptr<void, decltype(&CloseHandle)> token(rawToken, &CloseHandle);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes))
        return "Could not read the current account SID.";
    auto user = reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid;
    if (!owner || !EqualSid(owner, user))
        return "Storage is not owned by the current account.";
    alignas(DWORD) BYTE system[SECURITY_MAX_SID_SIZE];
    DWORD systemBytes = sizeof(system);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemBytes))
        return "Could not construct the local system SID.";
    if (!acl || acl->AceCount < 1 || acl->AceCount > 2)
        return "Storage does not have a bounded explicit DACL.";
    bool foundUser = false, foundSystem = false;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *entry = nullptr;
        if (!GetAce(acl, index, &entry))
            return "Could not inspect a storage access entry.";
        auto header = static_cast<ACE_HEADER *>(entry);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERITED_ACE))
            return "Storage contains an unexpected or inherited access entry.";
        auto allowed = static_cast<ACCESS_ALLOWED_ACE *>(entry);
        auto sid = reinterpret_cast<PSID>(&allowed->SidStart);
        bool isUser = EqualSid(sid, user), isSystem = EqualSid(sid, system);
        if (!isUser && !isSystem)
            return "Storage grants access to an account other than its owner or SYSTEM.";
        if ((allowed->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS)
            return "The storage owner/system access entry is incomplete.";
        if (directory && (header->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) !=
                             (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
            return "Private directory permissions will not protect newly created children.";
        foundUser |= isUser;
        foundSystem |= isSystem;
    }
    return foundUser && foundSystem ? QString{} : QString("A required storage account is absent.");
}
#endif

class StoragePermissionsTest : public QObject {
    Q_OBJECT
  private slots:
    void failedInitializationRemovesConnection() {
        QTemporaryDir data;
        QFile file(data.path() + "/reword.sqlite");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("synthetic invalid SQLite data");
        file.close();
        auto before = QSqlDatabase::connectionNames();
        before.sort();
        bool rejected = false;
        try {
            reword::Store store(data.path());
        } catch (const std::exception &) {
            rejected = true;
        }
        QVERIFY(rejected);
        auto after = QSqlDatabase::connectionNames();
        after.sort();
        QCOMPARE(after, before);
    }
    void existingSidecarGetsAccountPermissions() {
        QTemporaryDir data;
        {
            reword::Store store(data.path());
        }
        auto path = data.path() + "/reword.sqlite-shm";
        QFile sidecar(path);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write("synthetic unused SQLite sidecar");
        sidecar.close();
#ifndef _WIN32
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0644) == 0);
#endif
        {
            reword::Store store(data.path());
        }
        QVERIFY(QFile::exists(path));
#ifdef _WIN32
        auto error = securityError(path, false);
        QVERIFY2(error.isEmpty(), qPrintable(error));
#else
        struct stat state;
        QVERIFY(::stat(QFile::encodeName(path).constData(), &state) == 0);
        QCOMPARE(state.st_mode & 0777, mode_t(0600));
#endif
        QVERIFY(sidecar.open(QIODevice::ReadOnly));
        QCOMPARE(sidecar.readAll(), QByteArray("synthetic unused SQLite sidecar"));
    }
    void accountRestrictedDirectoryAndDatabase() {
        QTemporaryDir data;
        auto directory = data.path() + "/private-data";
        {
            reword::Store store(directory);
            store.setValue("synthetic", true);
        }
        for (const auto &path : {directory, directory + "/reword.sqlite"}) {
            bool isDirectory = path == directory;
#ifdef _WIN32
            auto error = securityError(path, isDirectory);
            QVERIFY2(error.isEmpty(), qPrintable(error));
#else
            struct stat state;
            QVERIFY(::stat(QFile::encodeName(path).constData(), &state) == 0);
            QCOMPARE(state.st_uid, geteuid());
            QCOMPARE(state.st_mode & 0777, mode_t(isDirectory ? 0700 : 0600));
#endif
        }
        reword::Store reopened(directory);
        QCOMPARE(reopened.value("synthetic").toBool(), true);
    }
    void parentTraversalIsRejected() {
        QTemporaryDir data;
        auto actual = data.path() + "/target";
        auto linked = data.path() + "/linked";
        QVERIFY(QDir().mkpath(actual + "/child"));
        QVERIFY(QDir().mkpath(actual + "/private-data"));
        QVERIFY(QDir().mkpath(data.path() + "/private-data"));
#ifdef _WIN32
        QVERIFY_EXCEPTION_THROWN(reword::Store{data.path() + "/target/../private-data"},
                                 std::exception);
#else
        QVERIFY(::symlink(QFile::encodeName(actual + "/child").constData(),
                          QFile::encodeName(linked).constData()) == 0);
        QVERIFY_EXCEPTION_THROWN(reword::Store{linked + "/../private-data"}, std::exception);
#endif
        QVERIFY(!QFileInfo::exists(actual + "/private-data/reword.sqlite"));
    }
    void linkedAncestorDoesNotCreateStorage() {
#ifdef _WIN32
        QSKIP("POSIX link fixture; native Windows ACLs are tested separately.");
#else
        QTemporaryDir data;
        auto actual = data.path() + "/actual";
        auto linked = data.path() + "/linked";
        QVERIFY(QDir().mkpath(actual));
        QVERIFY(::symlink(QFile::encodeName(actual).constData(),
                          QFile::encodeName(linked).constData()) == 0);
        QVERIFY_EXCEPTION_THROWN(reword::Store{linked + "/private-data"}, std::exception);
        QVERIFY(!QFileInfo::exists(actual + "/private-data"));
#endif
    }
    void nativeHardLinkDoesNotChangeOutsideAcl() {
#ifdef _WIN32
        QTemporaryDir data;
        auto outside = data.path() + "/outside.sqlite";
        QFile original(outside);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("synthetic outside data");
        original.close();
        auto snapshot = [](const QString &path) {
            DWORD bytes = 0;
            GetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()), DACL_SECURITY_INFORMATION,
                             nullptr, 0, &bytes);
            QByteArray result(bytes, '\0');
            if (!bytes || !GetFileSecurityW(
                              reinterpret_cast<LPCWSTR>(path.utf16()), DACL_SECURITY_INFORMATION,
                              reinterpret_cast<PSECURITY_DESCRIPTOR>(result.data()), bytes, &bytes))
                return QByteArray{};
            return result;
        };
        auto before = snapshot(outside);
        QVERIFY(!before.isEmpty());
        auto folder = data.path() + "/private-data";
        QVERIFY(QDir().mkpath(folder));
        auto linked = folder + "/reword.sqlite";
        QVERIFY(CreateHardLinkW(reinterpret_cast<LPCWSTR>(linked.utf16()),
                                reinterpret_cast<LPCWSTR>(outside.utf16()), nullptr));
        QVERIFY_EXCEPTION_THROWN(reword::Store{folder}, std::exception);
        QCOMPARE(snapshot(outside), before);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("synthetic outside data"));
#else
        QSKIP("Native NTFS hard-link fixture; POSIX aliases are tested separately.");
#endif
    }
    void hardLinkedDatabaseDoesNotChangeAlias() {
#ifdef _WIN32
        QSKIP("POSIX link fixture; native Windows ACLs are tested separately.");
#else
        QTemporaryDir data;
        auto outside = data.path() + "/outside.sqlite";
        QFile original(outside);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("synthetic outside data");
        original.close();
        QVERIFY(::chmod(QFile::encodeName(outside).constData(), 0644) == 0);
        auto folder = data.path() + "/private-data";
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(::link(QFile::encodeName(outside).constData(),
                       QFile::encodeName(folder + "/reword.sqlite").constData()) == 0);
        QVERIFY_EXCEPTION_THROWN(reword::Store{folder}, std::exception);
        struct stat state{};
        QVERIFY(::stat(QFile::encodeName(outside).constData(), &state) == 0);
        QCOMPARE(state.st_mode & 0777, mode_t(0644));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("synthetic outside data"));
#endif
    }
    void linkedPathsDoNotOpenStorage() {
#ifdef _WIN32
        QSKIP("POSIX link fixture; native Windows ACLs are tested separately.");
#else
        QTemporaryDir data;
        auto actual = data.path() + "/actual";
        auto linked = data.path() + "/linked";
        QVERIFY(QDir().mkpath(actual));
        QVERIFY(::symlink(QFile::encodeName(actual).constData(),
                          QFile::encodeName(linked).constData()) == 0);
        bool rejected = false;
        try {
            reword::Store store(linked);
        } catch (const std::exception &) {
            rejected = true;
        }
        QVERIFY(rejected);
        QVERIFY(!QFile::exists(actual + "/reword.sqlite"));
#endif
    }
};
QTEST_GUILESS_MAIN(StoragePermissionsTest)
#include "storage_permissions_test.moc"
