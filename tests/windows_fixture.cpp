#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <QString>
#include <string>
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
        } else if (s.rfind("text:", 0) == 0) {
            auto text = QString::fromUtf8(s.substr(5));
            SendMessageW(edit, EM_SETLIMITTEXT, 200001, 0);
            SetWindowTextW(edit, reinterpret_cast<LPCWSTR>(text.utf16()));
            SendMessageW(edit, EM_SETSEL, -1, -1);
        }
    }
}
int main() {
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
