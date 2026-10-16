# Linux package verification

This work closes two known packaging gaps before a new release is built:

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
acceptance. Host-addon dependency versions and complete third-party notices and
corresponding source delivery remain required before a public binary release.

The development AppImage is about 53 MiB. Both direct execution and the runtime's
`--appimage-extract-and-run` mode passed offscreen smoke launch with clean
environment/private D-Bus on the current machine. This does not establish
physical tray/display behavior or compatibility with older glibc systems.
