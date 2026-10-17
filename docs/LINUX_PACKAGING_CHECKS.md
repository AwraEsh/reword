# Linux package verification

The package audit checks runtime requirements before a new release is built:

1. Reject unresolved runtime libraries before creating a Debian package or AppImage.
2. Derive the minimum glibc from version needs of every bundled ELF object and
   the separately installed Fcitx addon, then use that value in Debian metadata.
3. Exercise the package command in isolated synthetic staging trees. One fixture
   reports an unresolved dependency; another requires GLIBC_2.39 despite the
   old hardcoded >=2.38 declaration. Neither fixture is a runnable release.
4. After these checks pass, audit and rebuild the real runtime closure, notices,
   source-delivery materials and final packages. Synthetic packaging checks do
   not establish a clean-machine launch or Wayland integration acceptance.

No system packages, source licensing choices or runtime user data are changed.

## Evidence so far

Both original defects were reproduced by running the package command in synthetic
staging trees. Eight functional checks now cover unresolved GUI/addon libraries,
non-C locales, versioned plugin dependencies, plugin and host-addon glibc requirements, ignoring provided symbol
versions, and unreadable version requirements. They run in CTest as `packaging`.
Independent read-only review found no remaining Critical/Important defects in
this scoped change.

A real development staging run inspected 240 ELF objects and required glibc 2.39;
its Debian metadata now declares `libc6 (>= 2.39)`. Both the staged application and
freshly extracted .deb launch with an empty inherited environment, their bundled
runtime paths, synthetic data and a private D-Bus session. This is not dpkg
installation, physical display/tray validation, or final binary-distribution
acceptance. Complete third-party notices and corresponding source delivery
remain required before a public binary release.

The development AppImage is about 53 MiB. Both direct execution and the runtime's
`--appimage-extract-and-run` mode passed offscreen smoke launch with clean
environment/private D-Bus on the current machine. This does not establish
physical tray/display behavior or compatibility with older glibc systems.


## Debian dependencies for the host addon — 2026-10-17

The separately loaded Fcitx addon cannot use the GUI bundle's private libraries.
The packager now runs dpkg-shlibdeps on that actual ELF, using Debian symbols
metadata from the build environment. Package names and version bounds are no
longer hardcoded to Qt 6.8, and required libstdc++/Fcitx libraries are included.
The Fcitx daemon and Qt SQLite plugin remain explicit runtime dependencies.
Missing metadata, failed inspection or missing/empty Depends output aborts the
build. The stronger libc floor from ELF needs or Debian symbols wins.

Building Debian artifacts therefore needs dpkg-dev (including dpkg-shlibdeps)
and valid Debian symbols/shlibs metadata for the resolved addon libraries. This
is a build requirement, not another tool an end user must install to run Reword.
Do not pass --ignore-missing-info to manufacture an incomplete dependency list.
The runtime manifest includes the derived host package constraints.

Four whole-command regressions failed before the change: a Qt 6.4 fixture was
forced to 6.8, metadata failures/invalid output were accepted, and a stronger
libc package constraint was lost. Fourteen packaging tests now pass, including
actual addon path, stable locale and no-addon independence. CTest's packaging
entry passes (3.67 seconds). Independent review found no Important defect.

An isolated real staging run using the primary build after PR #4 integration
inspected 240 ELF objects, built a temporary .deb and produced these host bounds:
Fcitx core/utils >=5.1.12, Qt Core >=6.8.2, Qt DBus/SQL >=6.1.2, libstdc++6 >=13.1,
libgcc-s1 >=3.0 and Hunspell. The combined .deb libc bound remains >=2.39.
A staged AppRun smoke launch exits 0 with empty inherited environment, private
D-Bus and synthetic data. Temporary staging was removed; previous development
packages were preserved. This does not prove dpkg installation or final-source
release/notice acceptance. Evidence is in ignored .deps/addon-real-package-audit.json.
