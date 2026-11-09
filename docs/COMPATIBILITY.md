# Compatibility evidence

This table describes tested fixtures, not blanket support for applications.

| Environment | Evidence | Limits |
|---|---|---|
| Debian 13 / GNOME X11 | Local GTK entry and end-to-end service probes have passed. | Plain single-line editable fields. Shared-desktop focus can be interrupted by other apps. Browser/editor matrix pending. |
| Debian X11 / GNOME Text Editor 48.3 | The user reported that live correction did not run. Metadata confirms a multiline editable text widget. | The current adapter supports single-line fields. GTK's full-buffer setter marks the edit irreversible, so a local range editing path is needed. |
| Debian X11 / Helium 0.12.4.1 | The user's failure reproduced in a temporary browser profile with an allowed rule and focused HTML input. The password control remained unchanged. | The tested browser exposed its window but no editable field to AT-SPI, including with forced renderer accessibility. |
| Debian X11 / Chrome 151.0.7922.173 | With forced renderer accessibility, a temporary browser profile exposed the focused HTML input. The typed typo remained unchanged. | The browser input did not expose the AT-SPI EditableText interface in that test. |
| Private Debian X11 / browser native messaging | Helium 0.12.4.1 and Google Chrome 151.0.7922.173 carried a local request and applied a guarded range edit in a plain input and textarea. The fixture discarded late replies after typing or focus changes and excluded password and read-only controls. | Fixed-response transport fixture. Browser correction with the Reword engine, rich web editors and installed browser profiles are still unverified. |
| Private Debian X11 / compiled Reword browser host | Helium and Chrome corrected English and Persian, passed alternating in-place retries, notified the form of range changes, honored page vetoes and restored a correction with the configured Undo shortcut. | Opt-in Linux host with a fixture extension. Desktop global shortcut delivery, ordinary browser profiles, rich fields and Windows registration remain open. |
| Private Debian X11 / physical browser Undo | The opt-in Helium and Chrome fixtures restored text using physical default and custom Undo while the GUI ran. The GUI retained Pause and regained Undo after restore or Pause. | Temporary browser profiles using the development host. Ordinary browser profiles and the Windows route remain open. |
| Private Debian X11 / unpacked Chromium extension | Helium and Chrome kept a site unchanged before registration, corrected and restored a word after registration, and stopped forwarding requests when the site was disabled. | Temporary profiles with the loopback test permission pre-granted. The browser permission prompt and normal profile installation are separate checks. |
| Linux browser host registration | The native command registered the host, updated its executable path and removed its own manifest in temporary browser data directories. Helium and Chrome launched the registered host. | Manual opt-in registration. Windows registration remains open. |
| Linux package browser runtime | Helium launched the host from an extracted `.deb` and Chrome launched it from an extracted AppImage, with developer library paths removed. Correction, Undo and the site gate passed. | Temporary browser profiles. AppImage registration requires a persistent extraction. Normal desktop installation and upgrade testing remain open. |
| Debian X11 / YukiGram 7.1.5.0 (Flatpak) | The user reported live correction did not run with Automatic selected. | The message field has no confirmed writable native path. |
| Private Debian X11 / IBus 1.5.32 | A fixed-word test engine corrected text in GNOME Text Editor 48.3 and preserved the reference text. In Helium, the test engine corrected its own preedit composition before committing it. The HTML password control kept its original text. | Diagnostic engine only. Helium sent key events and accepted composition but did not send surrounding text or report field purpose to the engine during the test. YukiGram and the production correction policies remain unverified on this path. |
| Private Debian X11 / compiled Reword IBus engine and GUI | GNOME Text Editor 48.3 corrected English and Persian. Independent language switches, alternating retry, Pause and Resume, per-app denial, 25 ms English key spacing, and live Undo passed. The native Undo also worked in a GTK single-line control before and after the editor. | Opt-in development target. Undo requires a current surrounding-text observation. Selected-text correction, live editing in Helium and YukiGram, and normal-desktop installation remain open. |
| Private Debian X11 / packaged Reword IBus component | IBus discovered the descriptor from an extracted `.deb` and launched the engine when selected. The packaged GUI and GNOME Text Editor passed the correction, language, retry, Undo, Pause and permission checks. | The private registry points at the extracted package. GNOME Settings discovery, normal desktop installation and upgrade, and selected-text correction in the editor remain open. |
| GNOME Shell consent | The user saw a native demo banner with Yes, Not now and No. The app's action handling, reminder and stale-response checks run on a private D-Bus test session. | The demo did not alter permissions. Windows banner appearance still needs a desktop check. |
| Controlled Linux Xvfb / Openbox | CI at 03069a1 passed build, four automated suites, GUI launch and three native GTK/service probe cycles. | Synthetic single-line fields; no physical lock-screen proof. |
| Linux without accessibility bus | Helper reports unavailable; internal workspace remains usable. | No native correction. |
| GNOME 48.7 Wayland / Fcitx 5.1.12 / Qt QLineEdit | Real keyboard input in a private compositor passed first/second/third, rejected-write retry, live Undo, code-span protection, pause, selected correction/history/Undo and password exclusion. | Synthetic field, explicit Qt Fcitx5 integration. Physical session and browser coverage pending. |
| GNOME Wayland / Fcitx5 / GTK3 GtkEntry | Real input passed first/second/third, live Undo, code-span protection, pause and password exclusion. | This GTK3 input module does not expose the selection anchor; selected-text correction is skipped. Use the workspace. |
| Windows x64 / hosted Windows runner | CI at 03069a1 passed build, automated tests, installed-file hashes, native-platform clean-PATH launch/uninstall and native Win32 field/password/hotkey probes. | Physical Windows 10/11 UI and browser/editor coverage pending. |
| Windows x64 / Chrome with the Reword GUI | CI at e674033 passed live correction, default and custom Undo, Pause during a browser correction, and native correction and Undo after switching to a Win32 editor. | Temporary profile and development build. Normal profile setup, physical Windows 10/11 use and browser installer integration remain open. |
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
cmake --build build --target reword reword-ibus reword_fixture
python3 scripts/test-native-linux.py --ibus-engine --ibus-gui --ibus-output ./local-reword-ibus-report
```

The driver uses a separate profile, store, X server and IBus daemon. It
checks the first three attempts, Pause and Resume, per-app denial, Undo
routing and normal typing when the adapter cannot verify a target.

The optional Debian IBus component is installed under `/usr/share/ibus/component`.
It is discovered by IBus and starts when selected as an input source. Debian
packages built with `REWORD_IBUS=ON` suggest `ibus` and `ibus-gtk4`. The
Debian package provides system input-source registration.
[IBus component registry](https://ibus.github.io/docs/ibus-1.5/IBusRegistry.html).

To exercise that registration and the packaged GUI in the private desktop:

~~~sh
dpkg-deb --extract dist/reword_0.1.0_amd64.deb ./local-deb-root
python3 scripts/test-native-linux.py --ibus-engine --ibus-gui \
  --ibus-package ./local-deb-root --ibus-output ./local-ibus-package-report
~~~

The driver puts a copy of the installed descriptor in the private IBus
registry and points its launcher at the extracted package. The GUI and
engine use the bundled libraries. Input-source selection is confined to
the private X server and IBus daemon.

To reproduce the browser transport fixture, use Node.js, Python 3, and the
installed browser in addition to the private X11 dependencies:

~~~sh
python3 scripts/test-native-linux.py --browser-output ./local-helium-report
python3 scripts/test-native-linux.py --browser google-chrome --browser-output ./local-chrome-report
~~~

The fixture loads an unpacked extension in a fresh profile. Helium uses the
command-line switch. Google Chrome uses the DevTools extension command over
a pipe because current branded Chrome builds disable that switch. The page
is served on loopback and the native messaging host exchanges fixed fixture
messages over standard I/O. The output contains outcomes and case names.
See [Chrome's extension loading notice](https://developer.chrome.com/blog/extension-news-june-2025)
and the [native messaging protocol](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

To run the same fixture with the compiled Linux host:

~~~sh
cmake -S . -B build -DREWORD_BROWSER_HOST=ON
cmake --build build --target reword-browser-host reword_browser_policy_fixture
python3 scripts/test-native-linux.py --browser-engine --browser-output ./local-helium-engine
python3 scripts/test-native-linux.py --browser google-chrome --browser-engine --browser-output ./local-chrome-engine
~~~

The fixture registers the host only in its own temporary profile and
uses a temporary Reword store. An opt-in CMake installation stages the
host and extension. Linux packages built with the browser host option
include both files and a launcher for the bundled libraries. Live
correction in this experiment is limited to plain
inputs and textareas of at most 384 UTF-16 code units.

~~~sh
cmake --install build --component Browser --prefix /path/to/staging
python3 scripts/test-native-linux.py --browser-engine --browser-extension --browser-output ./local-browser-extension
~~~

For a manual Linux development install, use the full opt-in CMake
installation so the host and dictionaries are in the same prefix. Keep
that prefix in a persistent directory. Find **Profile Path** on the
browser's version page and use the parent of the profile folder as the
browser data directory. For Chrome's
default `Default` profile this is usually `~/.config/google-chrome`.
Quit the browser, then run the host from the installed location.
[Chromium native messaging host location](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging#native-messaging-host-location).

~~~sh
/path/to/reword/bin/reword-browser-host --register-browser "/path/to/browser data"
~~~

In the browser's extension settings, enable Developer mode and choose
**Load unpacked**. Select the installed `share/reword/browser/chromium`
directory. Allow the browser in Reword's Applications settings and use
the extension button to enable the current site. Use the same register
command from a new persistent installation to update the host path.
To remove the native manifest, quit the browser and run
`reword-browser-host --unregister-browser "/path/to/browser data"`.
Remove the extension separately in browser settings.

### Register from the Linux packages

For a Debian package built with the browser host option, run
`reword-browser-host --register-browser "/path/to/browser data"` and load
`/opt/reword/usr/share/reword/browser/chromium` as the unpacked extension.
The registration is per user and is an explicit command.

For the AppImage, extract it once to a persistent directory to use the
browser feature. In that directory, run the bundled launcher and load
the extension from the extracted directory. The same AppImage can be
used normally for the desktop app.

~~~sh
/path/to/Reword-0.1.0-x86_64.AppImage --appimage-extract
./squashfs-root/usr/bin/reword-browser-launcher --register-browser "/path/to/browser data"
~~~

Choose `squashfs-root/usr/share/reword/browser/chromium` in the
extension manager. When moving or updating the extraction, extract to a
new persistent directory and rerun the registration command there.
Use `--unregister-browser` with the same launcher to remove the manifest.

To check the package without installing it or touching an existing browser
profile, extract the `.deb` and point the private fixture at its launcher.

~~~sh
dpkg-deb --extract dist/reword_0.1.0_amd64.deb ./local-deb-root
python3 scripts/test-native-linux.py --browser helium --browser-engine --browser-extension \
  --browser-host ./local-deb-root/opt/reword/usr/bin/reword-browser-launcher \
  --browser-output ./local-deb-browser-report
~~~

Use `--browser google-chrome` and the launcher under `squashfs-root` to
check the extracted AppImage the same way.

To exercise the physical X11 shortcut, build the GUI with the browser
host option enabled and run both modes in the private desktop.

~~~sh
cmake --build build --target reword
python3 scripts/test-native-linux.py --browser-engine --browser-x11 control --browser-output ./local-browser-physical-control
python3 scripts/test-native-linux.py --browser-engine --browser-x11 gui --browser-output ./local-browser-physical-gui
~~~

X11 probes require Xvfb, Xauth, Openbox, xprop, Python GI and AT-SPI's runtime
service. Wayland probes require GNOME Shell, Fcitx5, Python GI and the GTK/Qt
Fcitx5 client modules. They use private D-Bus sessions, displays and data folders.
The Wayland script checks that the remote-input service belongs to the compositor
it launched before sending any input. A fixture's results do not establish
physical lock-screen behavior or compatibility with arbitrary applications.
