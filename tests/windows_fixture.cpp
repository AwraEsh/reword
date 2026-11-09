#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <QString>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
static HWND edit = nullptr;
static std::string pending;
static LRESULT CALLBACK windowProc(HWND w, UINT message, WPARAM a, LPARAM b) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, message, a, b);
}
static void command(HWND window) {
    char buffer[8192];
    DWORD available = 0, count = 0;
    auto input = GetStdHandle(STD_INPUT_HANDLE);
    while (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && available) {
        if (!ReadFile(input, buffer, sizeof(buffer), &count, nullptr) || count == 0)
            break;
        pending.append(buffer, count);
        if (pending.size() > 1024 * 1024) {
            DestroyWindow(window);
            return;
        }
    }
    for (;;) {
        auto end = pending.find('\n');
        if (end == std::string::npos)
            break;
        auto s = pending.substr(0, end);
        pending.erase(0, end + 1);
        if (s == "quit") {
            DestroyWindow(window);
            return;
        }
        if (s == "password") {
            DestroyWindow(edit);
            edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"synthetic",
                                   WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 10, 10,
                                   460, 30, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetFocus(edit);
        } else if (s == "sync") {
            std::fputs("READY\n", stdout);
            std::fflush(stdout);
        } else if (s == "snapshot") {
            wchar_t text[64]{};
            GetWindowTextW(edit, text, 64);
            DWORD caret = 0;
            SendMessageW(edit, EM_GETSEL, 0, reinterpret_cast<LPARAM>(&caret));
            std::printf("TEXT:%lu:%s\n", caret, QString::fromWCharArray(text).toUtf8().constData());
            std::fflush(stdout);
        } else if (s.rfind("text:", 0) == 0) {
            auto text = QString::fromUtf8(s.substr(5));
            SendMessageW(edit, EM_SETLIMITTEXT, 200001, 0);
            SetWindowTextW(edit, reinterpret_cast<LPCWSTR>(text.utf16()));
            SendMessageW(edit, EM_SETSEL, WPARAM(text.size()), LPARAM(text.size()));
        }
    }
}
static bool clickFixture(DWORD expectedProcess) {
    struct Search { DWORD process; HWND window; } search{expectedProcess, nullptr};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        auto &search = *reinterpret_cast<Search *>(context);
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        wchar_t name[64]{};
        if (process == search.process && IsWindowVisible(window) && GetClassNameW(window, name, 64) &&
            std::wstring_view(name) == L"RewordNativeFixture") {
            search.window = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.window)
        return false;
    const auto field = FindWindowExW(search.window, nullptr, L"Edit", nullptr);
    RECT bounds{};
    if (!field || !GetWindowRect(field, &bounds) ||
        !SetWindowPos(search.window, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        return false;
    const POINT point{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    INPUT click[2]{};
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const bool sent = GetAncestor(WindowFromPoint(point), GA_ROOT) == search.window &&
        SetCursorPos(point.x, point.y) && SendInput(2, click, sizeof(INPUT)) == 2;
    for (int attempt = 0; sent && GetForegroundWindow() != search.window && attempt < 25; ++attempt)
        Sleep(10);
    const bool focused = GetForegroundWindow() == search.window;
    SetWindowPos(search.window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return sent && focused;
}

static int sendTestInput(DWORD expectedProcess, const QString &action) {
    if (expectedProcess && action == "focus-fixture")
        return clickFixture(expectedProcess) ? 0 : 1;
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    if (!expectedProcess || foreground != expectedProcess)
        return 1;
    if (action == "foreground")
        return 0;
    std::vector<WORD> chord;
    if (action == "space")
        chord = {VK_SPACE};
    else if (action == "undo")
        chord = {VK_CONTROL, VK_MENU, 'Z'};
    else if (action == "custom-undo")
        chord = {VK_CONTROL, VK_MENU, 'K'};
    else if (action == "pause")
        chord = {VK_CONTROL, VK_MENU, VK_SHIFT, 'P'};
    else
        return 2;
    INPUT keys[8]{};
    size_t count = 0;
    for (WORD key : chord) {
        keys[count].type = INPUT_KEYBOARD;
        keys[count++].ki.wVk = key;
    }
    for (auto key = chord.rbegin(); key != chord.rend(); ++key) {
        keys[count].type = INPUT_KEYBOARD;
        keys[count].ki.dwFlags = KEYEVENTF_KEYUP;
        keys[count++].ki.wVk = *key;
    }
    return SendInput(UINT(count), keys, sizeof(INPUT)) == count ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--input")
        return sendTestInput(QString::fromLatin1(argv[2]).toUInt(), QString::fromLatin1(argv[3]));
    if (argc != 1)
        return 2;
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"RewordNativeFixture";
    RegisterClassW(&cls);
    auto window = CreateWindowExW(0, cls.lpszClassName, L"Reword synthetic integration fixture",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, 30, 30, 520, 160, nullptr,
                                  nullptr, cls.hInstance, nullptr);
    edit =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"teh ", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                        10, 10, 460, 30, window, nullptr, cls.hInstance, nullptr);
    SendMessageW(edit, EM_SETSEL, -1, -1);
    SetForegroundWindow(window);
    SetFocus(edit);
    SetTimer(window, 1, 25, nullptr);
    MSG message;
    auto started = GetTickCount64();
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER) {
            command(window);
            if (GetTickCount64() - started > 45000)
                DestroyWindow(window);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
