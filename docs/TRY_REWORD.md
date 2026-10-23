# Try Reword Preview

Reword Preview is available for testing. Keep a copy of important text while
trying direct correction. This is not the final stable release.

## Windows

[Download the Windows installer archive](https://github.com/AwraEsh/reword/actions/runs/39757127469/artifacts/12415994422),
extract it, and run the included setup executable. GitHub may require sign-in;
CI downloads expire. This build passed automated install, launch, uninstall and
native field/shortcut checks on the Windows runner. Ordinary Windows 10/11
desktop use still needs broader testing.

## This Debian computer

Open **Reword Preview** from the application menu. It is installed only for your
account, at `~/.local/opt/reword/reword-0.1.0-preview.AppImage`. Startup on login
is not enabled. The launcher uses extract-and-run, so FUSE is not required.

The development packages are also in the build checkout's `dist` directory:
`Reword-0.1.0-x86_64.AppImage` and `reword_0.1.0_amd64.deb`. The bundled GUI
needs glibc 2.39 or newer. System-wide Debian installation has not been tested.
Wayland typing integration additionally requires the Fcitx5 addon and host setup;
the AppImage alone does not install an input-method addon.

## First use

Paste a short passage into the workspace and choose **Correct text**. Language
and rule switches are in Settings. To allow direct correction in a supported
application, choose **Yes** when asked; **Not now** and **No** leave it disabled.
Use **Pause** before sensitive work. Unsupported fields are skipped.

Default shortcuts are Ctrl+Alt+R for selected text, Ctrl+Alt+Z for guarded Undo,
and Ctrl+Alt+Shift+P for Pause. You can change them in Settings. Copy the latest
original from session recovery if Undo is unavailable.

## What has been checked

The integrated build passes all 13 local test targets and Linux/Windows CI.
AppDir, extracted Debian package and AppImage pass clean-environment launch
checks on this Debian host. The local active workload exercised eight live
first/second/third correction and Undo sequences plus 117 selected corrections,
but stopped on its CPU budget after 73.685 seconds: app CPU was 14.18% against
a 10% test target. Word latency p95 was 33.2 ms across 16 samples. This is partial
evidence, not a completed stability run. Tray behavior, broad Wayland use and
long-run acceptance remain open.
