# Compatibility evidence

| Environment | Current evidence | Limits |
|---|---|---|
| Debian 13 / GNOME X11 | Local build and repeated core/UI tests pass. Real GTK entry probe passed. | Single-line plain editable fields only. Full browser/editor matrix remains pending. |
| Linux without accessibility bus | Helper reports unavailable; internal workspace remains usable. | No native correction. |
| GNOME Wayland | Fcitx5 addon compiles; async correction/confirmation/Undo code exists. | Real Wayland login and application tests pending. Requires host Fcitx integration. |
| Windows 10/11 x64 | UI Automation adapter, message-window hotkeys and NSIS build recipe exist. | Remote build rerun and real desktop testing pending. |
| Internal workspace | Real Qt Quick test confirms correction/history and stale-result rejection. | Rules are conservative, not semantic rewriting. |

AT-SPI supports GtkEntry-style single-line fields, including TEXT roles marked
SINGLE_LINE. Rich/multiline fields are deliberately not rewritten wholesale.
Windows requires a non-password Edit control with ValuePattern and a bounded
TextPattern document. Missing capabilities fail closed.

Fcitx application names are separate consent entries (`fcitx:` prefix) because
input-method program identity is not the same interface as executable discovery.
No existing executable permission is silently reused for an input-method client.

Native calls validate before writing, but desktop editing APIs do not provide a
universal cross-process atomic compare-and-swap. Compatibility must be measured
in each application; unsupported behavior must not be advertised as verified.
