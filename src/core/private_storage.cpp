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
#include <memory>
#include <stdexcept>
#include <vector>
namespace reword {
namespace {
[[noreturn]] void failed() {
    throw std::runtime_error("Cannot establish private local storage access.");
}
} // namespace
void restrictStoragePath(const QString &path, bool directory) {
#ifdef _WIN32
    HANDLE raw = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()), WRITE_DAC | WRITE_OWNER | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        failed();
    std::unique_ptr<void, decltype(&CloseHandle)> file(raw, &CloseHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
        failed();
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
        failed();
    std::unique_ptr<void, decltype(&CloseHandle)> token(rawToken, &CloseHandle);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (!bytes)
        failed();
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes))
        failed();
    auto user = reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid;
    alignas(DWORD) BYTE system[SECURITY_MAX_SID_SIZE];
    DWORD systemBytes = sizeof(system);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemBytes))
        failed();
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
    if (SetEntriesInAclW(2, entries, nullptr, &rawAcl) != ERROR_SUCCESS)
        failed();
    std::unique_ptr<void, decltype(&LocalFree)> acl(rawAcl, &LocalFree);
    if (SetSecurityInfo(file.get(), SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                            PROTECTED_DACL_SECURITY_INFORMATION,
                        user, nullptr, rawAcl, nullptr) != ERROR_SUCCESS)
        failed();
#else
    int fd = ::open(QFile::encodeName(path).constData(),
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (directory ? O_DIRECTORY : 0));
    if (fd < 0)
        failed();
    struct stat state{};
    bool valid = ::fstat(fd, &state) == 0 && state.st_uid == ::geteuid() &&
                 (directory ? S_ISDIR(state.st_mode) : S_ISREG(state.st_mode));
    bool protectedAccess = valid && ::fchmod(fd, directory ? 0700 : 0600) == 0;
    ::close(fd);
    if (!protectedAccess)
        failed();
#endif
}
} // namespace reword
