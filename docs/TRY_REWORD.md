# Try Reword Preview

Reword Preview lets you try the writing workspace and correction in supported
apps. Keep a copy of important text when testing direct correction.

## Windows

Open the [integration build runs](https://github.com/AwraEsh/reword/actions/workflows/build.yml?query=branch%3Acodex%2Fusable-build-integration).
From a successful run, download **Reword-Windows**, extract the archive and
run `Reword-Setup-0.1.0.exe`. GitHub may require sign-in and CI downloads expire.
The automated Windows checks cover installation, launch, uninstall and standard
editing controls. Ordinary Windows 10/11 desktop use still needs broader testing.

### Try live correction in Chrome or Chromium

In a build with browser support, select **Register Chrome connection** or
**Register Chromium connection** in setup. In the browser's extensions page,
enable **Developer mode**, choose **Load unpacked**, and select the
`browser\chromium` folder inside the Reword installation directory. The default
installation is `%LOCALAPPDATA%\Programs\Reword`. Run Reword, allow the browser
in **Applications**, then enable each site from the extension button.

Quit Reword and the browser before running setup again. Setup reuses the
recorded installation directory and keeps existing browser registrations.
Remove the extension in the browser's extensions page when you no longer use it.

## Debian-based Linux

Development packages are built into `dist`. The bundled GUI requires x86-64
and glibc 2.39 or newer. To launch the AppImage without FUSE:

```sh
chmod +x Reword-0.1.0-x86_64.AppImage
./Reword-0.1.0-x86_64.AppImage --appimage-extract-and-run
```

If you already have the per-user preview launcher, open **Reword Preview**
from the application menu. The Debian package has passed extraction and
launch checks. A system-wide upgrade has yet to be verified.
Wayland typing integration additionally requires the Fcitx5 addon and host setup;
the AppImage alone does not install an input-method addon.

### Try live correction in GNOME Text Editor on X11

Use a Debian package built with `REWORD_IBUS=ON` on an X11 session that
uses IBus. Debian provides the IBus daemon and GTK4 integration in `ibus`
and `ibus-gtk4`. Sign out and back in after installing the package, then
look for **Reword (X11)** under English in **Settings → Keyboard → Input
Sources**. Select that source, run Reword and allow GNOME Text Editor in
**Applications**. To stop using the engine, switch back to your previous
input source. Reword uses the same application rules after switching.
[GNOME keyboard settings](https://help.gnome.org/gnome-help/keyboard-layouts.html).

### Try live correction in Chrome or Helium

The experimental browser integration was tested on Linux X11. It requires a build with
`REWORD_BROWSER_HOST=ON`. Register the host and load the bundled extension,
then allow the browser in **Applications** and turn on the site from the
extension button. The AppImage needs a persistent extraction for browser
registration. See the [Linux package setup steps](COMPATIBILITY.md#register-from-the-linux-packages).

## First use

Paste a short passage into the workspace and choose **Correct text**. Language
and rule switches are in Settings. To allow direct correction in a supported
application, choose **Yes** when asked; **Not now** and **No** leave it disabled.
Use **Pause** before sensitive work. Unsupported fields are skipped.

On X11, Reword requests desktop accessibility when it starts so that applications
can be discovered. This does not enable the screen reader or allow applications
automatically. If activation fails, the platform status explains that discovery
is unavailable. The internal writing workspace remains available.
An application can be detected while its fields remain unsupported. Reword
corrects only fields it can safely read and replace.

Consent uses system notifications without bringing Reword to
the front. GNOME notifications offer **Yes**, **Not now** and **No**. Requests
you ignore stay pending in Reword and are reminded after 15 minutes. Returning
to Reword shows the pending choice. Notifications request seven seconds of
display time; the desktop controls dismissal and fading.

Default shortcuts are Ctrl+Alt+R for selected text, Ctrl+Alt+Z for guarded Undo,
and Ctrl+Alt+Shift+P for Pause. You can change them in Settings. Copy the latest
original from session recovery if Undo is unavailable.

## Choose what Reword does in each app

In builds with correction modes, open **Applications** and choose **Automatic**
to allow live corrections,
**Shortcut only** to correct selected text on request, or **Disabled** to turn
off both. **Remove rule** forgets the saved permission. If Reword detects the
app again, it needs permission before correcting text there.

## What has been checked

See the [compatibility notes](COMPATIBILITY.md) for the specific fields and
platforms checked so far. GNOME displayed all three action buttons in a desktop
notification preview. Chrome and Helium corrected and restored text in
temporary test profiles. Regular browser profiles, Telegram, broader Wayland
use and extended stability testing remain open.
