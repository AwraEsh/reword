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


## Runtime provenance and QML deployment — 2026-10-17

Every copied runtime file now has a package origin verified against installed
Debian md5sums or downloaded .deb control metadata; staged bytes receive an
independent SHA256 check. Rootless files cannot borrow an installed package's
version. Exact Source fields preserve epochs and binary-rebuild differences.
Registered GPU-library diversions resolve to original package metadata, still
requiring the original checksum. Equivalent multiarch owners are allowed only
for a shared copyright notice, never to guess the origin of a runtime file.

The packager copies the package's own verified copyright path (including an
explicit shared-document link), not an arbitrary sibling notice. A checksum for
that path must still match through a link. Common license texts are also verified
against package metadata and fingerprinted. Missing origin, modified bytes,
missing notice or an escaping/conflicting destination stops installer creation.
All published manifest paths are relative to the bundle.

QML deployment uses qmlimportscanner from the build Qt SDK, rather than copying
the entire QtQuick tree. Nested formal modules are copied only when scanned;
Qt Creator designer assets are excluded. A required import from the app, a
module's qmldir or its deployed QML/JS overrides optional uses elsewhere. Absent
optional platform styles are allowed; malformed results, missing required
imports and paths outside the import root fail. Input context plugins are limited
to compose, IBus and Fcitx5 when present. Legacy Fcitx/UIM/unused virtual-keyboard
providers are excluded from this English/Persian desktop bundle.

Build tooling needs Python 3.10+, dpkg-query/dpkg-deb, package checksum metadata
and qmlimportscanner; end users do not need Python or the build tools. Corresponding
source archive delivery and Windows-specific materials remain separate gates.
The provenance resolver trusts the build host's package database and downloaded
package archives; it is not an authentication boundary against a compromised
build host. No library is modified by this audit.

Forty-seven functional checks pass (8.59 seconds), including four initial missing
origin/notice/modified-file/manifest regressions, real tiny .deb fixtures,
source epochs, shared multiarch copyright, diversions and required QML imports
masked by optional use. The QML/transitive cases were observed failing before
fixes. CTest packaging passes (8.82 seconds). Independent review caught the
transitive-import issue; final follow-up review is pending at this edit.

A final isolated real staging run uses primary source b48cdc5 (application SHA256
f82346c50b0dd14f582064706f8fc9a08a9284122066136fbcd62d3bb3f874f9).
It records 524 runtime files, 134 binary package components, 78 source identities,
21 QML modules and 17 common license texts. There are 197 ELF objects including
the application/helper/addon, down from the earlier unfiltered inventory's 240.
The old inventory had 861 runtime files and 155 components. The temporary .deb
builds, and AppRun launches with an empty inherited environment, bundled runtime,
private D-Bus and synthetic data (exit 0). Prior development installers were
preserved; temporary staging was removed. These are not dpkg installation,
all-page interaction, final AppImage acceptance or completed source delivery.

Evidence: ignored `.deps/provenance-final-tests.log`, `provenance-final-ctest.log`,
`provenance-final-real-audit.log` and `provenance-real-package-audit.json`.
