#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "private_storage.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace reword {
namespace {
[[noreturn]] void failed(const char *stage = "Cannot establish private local storage access.",
                         unsigned long code = 0) {
    throw std::runtime_error(std::string(stage) +
                             (code ? " System error " + std::to_string(code) + "." : ""));
}
} // namespace
void validateStorageLocation(const QString &path) {
    if (QDir::fromNativeSeparators(path).split('/').contains(".."))
        failed();
    auto current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (;;) {
#ifdef _WIN32
        auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            failed("Linked storage locations are not supported.");
#else
        if (QFileInfo(current).isSymLink())
            failed();
#endif
        auto parent = QFileInfo(current).absolutePath();
        if (parent == current)
            break;
        current = parent;
    }
}
static void accessStoragePath(const QString &path, bool directory, bool apply) {
    validateStorageLocation(path);
#ifdef _WIN32
    HANDLE raw =
        CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                    FILE_READ_ATTRIBUTES | (apply ? READ_CONTROL | WRITE_DAC | WRITE_OWNER : 0),
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        failed("Cannot open the private storage object.", GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> file(raw, &CloseHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info))
        failed("Cannot inspect the private storage object.", GetLastError());
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (!directory && info.nNumberOfLinks != 1))
        failed("Private storage object type or link count is unsafe.");
    if (!apply)
        return;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
        failed("Cannot inspect the storage account token.", GetLastError());
    std::unique_ptr<void, decltype(&CloseHandle)> token(rawToken, &CloseHandle);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (!bytes)
        failed();
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes))
        failed("Cannot read the storage account identity.", GetLastError());
    auto user = reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid;
    alignas(DWORD) BYTE system[SECURITY_MAX_SID_SIZE];
    DWORD systemBytes = sizeof(system);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemBytes))
        failed("Cannot construct the storage system identity.", GetLastError());
    EXPLICIT_ACCESSW entries[2]{};
    PSID accounts[] = {user, system};
    for (int i = 0; i < 2; ++i) {
        entries[i].grfAccessPermissions = FILE_ALL_ACCESS;
        entries[i].grfAccessMode = SET_ACCESS;
        entries[i].grfInheritance = directory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        entries[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entries[i].Trustee.TrusteeType = TRUSTEE_IS_USER;
        entries[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(accounts[i]);
    }
    PACL rawAcl = nullptr;
    auto aclStatus = SetEntriesInAclW(2, entries, nullptr, &rawAcl);
    if (aclStatus != ERROR_SUCCESS)
        failed("Cannot construct the private storage access list.", aclStatus);
    std::unique_ptr<void, decltype(&LocalFree)> acl(rawAcl, &LocalFree);
    auto status = SetSecurityInfo(file.get(), SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                                      PROTECTED_DACL_SECURITY_INFORMATION,
                                  user, nullptr, rawAcl, nullptr);
    if (status != ERROR_SUCCESS)
        failed("Cannot restrict the private storage access list.", status);
#else
    int fd = ::open(QFile::encodeName(path).constData(),
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (directory ? O_DIRECTORY : 0));
    if (fd < 0)
        failed();
    struct stat state{};
    bool valid =
        ::fstat(fd, &state) == 0 && state.st_uid == ::geteuid() &&
        (directory ? S_ISDIR(state.st_mode) : (S_ISREG(state.st_mode) && state.st_nlink == 1));
    bool protectedAccess = valid && (!apply || ::fchmod(fd, directory ? 0700 : 0600) == 0);
    ::close(fd);
    if (!protectedAccess)
        failed();
#endif
}
void validateExistingStorageFile(const QString &path) {
    accessStoragePath(path, false, false);
}
void restrictStoragePath(const QString &path, bool directory) {
    accessStoragePath(path, directory, true);
}
} // namespace reword
