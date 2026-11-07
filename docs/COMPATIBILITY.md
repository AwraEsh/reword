# Compatibility evidence

This table describes tested fixtures, not blanket support for applications.

| Environment | Evidence | Limits |
|---|---|---|
| Debian 13 / GNOME X11 | Local GTK entry and end-to-end service probes have passed. | Plain single-line editable fields. Shared-desktop focus can be interrupted by other apps. Browser/editor matrix pending. |
| Debian X11 / GNOME Text Editor 48.3 | The user reported that live correction did not run. Metadata confirms a multiline editable text widget. | The current adapter supports single-line fields. GTK's full-buffer setter marks the edit irreversible, so a local range editing path is needed. |
| Debian X11 / Helium 0.12.4.1 | The user's failure reproduced in a temporary browser profile with an allowed rule and focused HTML input. The password control remained unchanged. | The tested browser exposed its window but no editable field to AT-SPI, including with forced renderer accessibility. |
| Debian X11 / Chrome 151.0.7922.173 | With forced renderer accessibility, a temporary browser profile exposed the focused HTML input. The typed typo remained unchanged. | The browser input did not expose the AT-SPI EditableText interface in that test. |
| Debian X11 / YukiGram 7.1.5.0 (Flatpak) | The user reported live correction did not run with Automatic selected. | The message field has no confirmed writable native path. |
| Private Debian X11 / IBus 1.5.32 | A fixed-word test engine corrected text in GNOME Text Editor 48.3 and preserved the reference text. In Helium, the test engine corrected its own preedit composition before committing it. The HTML password control kept its original text. | Diagnostic engine only. Helium sent key events and accepted composition but did not send surrounding text or report field purpose to the engine during the test. YukiGram and the production correction policies remain unverified on this path. |
| Private Debian X11 / compiled Reword IBus engine | GNOME Text Editor 48.3 corrected English and Persian with the shared engine. Both directions of independent language disabling passed. The alternating retry cycle, pause, per-app denial and the English 25 ms key-spacing case passed. | Opt-in development target. An earlier intermittent first-word failure did not recur in the latest runs. Helium, YukiGram, selected-text correction, Undo and normal-desktop installation remain open for this engine. |
| GNOME Shell consent | The user saw a native demo banner with Yes, Not now and No. The app's action handling, reminder and stale-response checks run on a private D-Bus test session. | The demo did not alter permissions. Windows banner appearance still needs a desktop check. |
| Controlled Linux Xvfb / Openbox | CI at 03069a1 passed build, four automated suites, GUI launch and three native GTK/service probe cycles. | Synthetic single-line fields; no physical lock-screen proof. |
| Linux without accessibility bus | Helper reports unavailable; internal workspace remains usable. | No native correction. |
| GNOME 48.7 Wayland / Fcitx 5.1.12 / Qt QLineEdit | Real keyboard input in a private compositor passed first/second/third, rejected-write retry, live Undo, code-span protection, pause, selected correction/history/Undo and password exclusion. | Synthetic field, explicit Qt Fcitx5 integration. Physical session and browser coverage pending. |
| GNOME Wayland / Fcitx5 / GTK3 GtkEntry | Real input passed first/second/third, live Undo, code-span protection, pause and password exclusion. | This GTK3 input module does not expose the selection anchor; selected-text correction is skipped. Use the workspace. |
| Windows x64 / hosted Windows runner | CI at 03069a1 passed build, automated tests, installed-file hashes, native-platform clean-PATH launch/uninstall and native Win32 field/password/hotkey probes. | Physical Windows 10/11 UI and browser/editor coverage pending. |
| Internal workspace | Qt Quick tests verify correction/history, stale-result rejection, limits, pause/cancellation and saved markup never requesting HTTP/HTTPS. | Offline rules are conservative; no semantic rewriting. |

## Native boundaries

AT-SPI supports GtkEntry-style single-line fields, including TEXT roles marked
SINGLE_LINE. Rich/multiline fields are not rewritten wholesale. Windows requires
a non-password Edit control with bounded, verifiable text access, using UI
Automation or the standard Win32 Edit fallback. Missing capabilities fail closed.

[GTK's AT-SPI text-view implementation](https://raw.githubusercontent.com/GNOME/gtk/4.18.6/gtk/a11y/gtkatspieditabletext.c)
uses the whole-buffer setter for replacement. [GTK marks that setter as
irreversible](https://raw.githubusercontent.com/GNOME/gtk/4.18.6/gtk/gtktextbuffer.c).

Fcitx application names have separate consent entries (`fcitx:` prefix). Existing
executable permissions are not silently reused for input-method clients.
Wayland integration requires the host's Fcitx5 daemon and the appropriate client
input module. When multiple GTK Fcitx generations are installed, the explicit
`fcitx5` module avoids selecting the legacy module by mistake.

Native calls validate before writing, but desktop editing APIs do not offer a
universal cross-process atomic compare-and-swap. Every supported application
needs its own compatibility checks. No field can be assumed safe solely because
its application was allowed.

The X11 and Windows adapters recheck app policy after validating the native
target. Slow or denied authorization leaves the field untouched. X11 drops
edits when it observes a focus or target change during authorization. Windows
checks the target again before requesting final authorization. Once an edit
has reached the desktop provider, a later policy change cannot recall it.

## Reproduce the isolated probes

Build first, then run on a development machine with the named dependencies:

```sh
python3 scripts/test-native-linux.py
/usr/bin/python3 scripts/test-native-wayland.py --fixture qt
/usr/bin/python3 scripts/test-native-wayland.py --fixture gtk
```

The optional IBus experiment requires IBus and its GTK modules, GNOME Text
Editor, Helium, and Python GObject bindings. Run it with the same private X11
dependencies as the native GTK probe:

```sh
python3 scripts/test-native-linux.py --ibus-output ./local-ibus-report
```

The probe selects a fixed-word diagnostic engine on a separate IBus daemon
inside the private X11 session. It verifies the synthetic file saved by the
editor and the test page's plain and password fields. The production app does
not use the diagnostic engine. IBus provides separate methods for
[editing surrounding text and committing text](https://ibus.github.io/docs/ibus-1.4/IBusEngine.html).

For the compiled Reword IBus experiment, install the IBus development headers
and build with the existing X11 toolchain. On Debian the headers are in
`libibus-1.0-dev`.

```sh
cmake -S . -B build -DREWORD_IBUS=ON
cmake --build build --target reword-ibus reword_fixture
python3 scripts/test-native-linux.py --ibus-engine --ibus-output ./local-reword-ibus-report
```

The driver uses a separate profile, store, X server and IBus daemon. It
checks the first three attempts, pause, per-app denial and normal typing
when the adapter cannot verify a target.

X11 probes require Xvfb, Xauth, Openbox, xprop, Python GI and AT-SPI's runtime
service. Wayland probes require GNOME Shell, Fcitx5, Python GI and the GTK/Qt
Fcitx5 client modules. They use private D-Bus sessions, displays and data folders.
The Wayland script checks that the remote-input service belongs to the compositor
it launched before sending any input. A fixture's results do not establish
physical lock-screen behavior or compatibility with arbitrary applications.
