# Compatibility evidence

This table describes tested fixtures, not blanket support for applications.

| Environment | Evidence | Limits |
|---|---|---|
| Debian 13 / GNOME X11 | Local GTK entry and end-to-end service probes have passed. | Plain single-line editable fields. Shared-desktop focus can be interrupted by other apps. Browser/editor matrix pending. |
| Controlled Linux Xvfb | Core/UI/service tests pass in Linux CI. | Native GTK focus failed on bare Xvfb; the updated harness adds a window manager. Fresh CI evidence pending. |
| Linux without accessibility bus | Helper reports unavailable; internal workspace remains usable. | No native correction. |
| GNOME 48.7 Wayland / Fcitx 5.1.12 / Qt QLineEdit | Real keyboard input in a private compositor passed first/second/third, rejected-write retry, live Undo, code-span protection, pause, selected correction/history/Undo and password exclusion. | Synthetic field, explicit Qt Fcitx5 integration. Physical session and browser coverage pending. |
| GNOME Wayland / Fcitx5 / GTK3 GtkEntry | Real input passed first/second/third, live Undo, code-span protection, pause and password exclusion. | This GTK3 input module does not expose the selection anchor; selected-text correction is skipped. Use the workspace. |
| Windows x64 / hosted Windows runner | CI at c02bf5d passed build, automated tests, NSIS packaging and native Win32 field/password/hotkey probes. | Clean installer launch/uninstall test newly added and pending. Physical Windows 10/11 UI and browser/editor coverage pending. |
| Internal workspace | Qt Quick tests verify correction/history, stale-result rejection, limits, pause/cancellation and saved markup never requesting HTTP/HTTPS. | Offline rules are conservative; no semantic rewriting. |

## Native boundaries

AT-SPI supports GtkEntry-style single-line fields, including TEXT roles marked
SINGLE_LINE. Rich/multiline fields are not rewritten wholesale. Windows requires
a non-password Edit control with bounded, verifiable text access, using UI
Automation or the standard Win32 Edit fallback. Missing capabilities fail closed.

Fcitx application names have separate consent entries (`fcitx:` prefix). Existing
executable permissions are not silently reused for input-method clients.
Wayland integration requires the host's Fcitx5 daemon and the appropriate client
input module. When multiple GTK Fcitx generations are installed, the explicit
`fcitx5` module avoids selecting the legacy module by mistake.

Native calls validate before writing, but desktop editing APIs do not offer a
universal cross-process atomic compare-and-swap. Every supported application
needs its own compatibility checks. No field can be assumed safe solely because
its application was allowed.

## Reproduce the isolated probes

Build first, then run on a development machine with the named dependencies:

```sh
python3 scripts/test-native-linux.py
/usr/bin/python3 scripts/test-native-wayland.py --fixture qt
/usr/bin/python3 scripts/test-native-wayland.py --fixture gtk
```

X11 probes require Xvfb, Xauth, Openbox, xprop, Python GI and AT-SPI's runtime
service. Wayland probes require GNOME Shell, Fcitx5, Python GI and the GTK/Qt
Fcitx5 client modules. They use private D-Bus sessions, displays and data folders.
The Wayland script checks that the remote-input service belongs to the compositor
it launched before sending any input. A fixture's results do not establish
physical lock-screen behavior or compatibility with arbitrary applications.
