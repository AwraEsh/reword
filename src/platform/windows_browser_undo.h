#pragma once

#include <QCryptographicHash>
#include <QFileInfo>
#include <string>
#include <windows.h>

namespace reword {
inline constexpr wchar_t WindowsHotkeyWindowClass[] = L"RewordHotkeyWindow";
inline constexpr ULONG_PTR BrowserUndoCopyTag = 0x52574231; // RWB1
inline constexpr ULONGLONG BrowserUndoLifetimeMs = 30000;
inline constexpr UINT BrowserUndoMessageTimeoutMs = 350;

struct WindowsBrowserUndoMessage {
    ULONGLONG issued;
    ULONGLONG target;
    ULONGLONG browserPid;
};

inline std::wstring windowsUndoEndpoint(const QString &directory) {
    if (directory.isEmpty())
        return {};
    const auto path = QFileInfo(directory).absoluteFilePath().toCaseFolded().toUtf8();
    return (QStringLiteral("Reword Undo ") + QString::fromLatin1(
        QCryptographicHash::hash(path, QCryptographicHash::Sha256).toHex().left(24))).toStdWString();
}

inline DWORD windowProcess(HWND window) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process;
}
}
