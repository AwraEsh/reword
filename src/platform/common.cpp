#include "adapter.h"
#include <QFileInfo>
namespace reword {
bool browserApp(const QString &id) {
    auto n = QFileInfo(id).fileName().toLower();
    if (n.startsWith("fcitx:"))
        n.remove(0, 6);
    return n.contains("firefox") || n.contains("chrome") || n.contains("chromium") ||
           n.contains("brave") || n.contains("msedge") || n.contains("vivaldi") ||
           n == "helium" || n == "helium.exe";
}
bool restrictedApp(const QString &id) {
    auto n = QFileInfo(id).fileName().toLower();
    for (const auto &s :
         {"terminal", "konsole", "kitty", "alacritty", "wezterm", "code", "codium", "idea",
          "pycharm", "keepass", "bitwarden", "1password", "emacs", "vim", "reword"})
        if (n.contains(s))
            return true;
    return n == "cmd.exe" || n == "powershell.exe" || n == "pwsh.exe" || n == "wt.exe";
}
} // namespace reword
