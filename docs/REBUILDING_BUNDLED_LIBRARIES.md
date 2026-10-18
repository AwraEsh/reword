# Rebuilding bundled Linux libraries

These instructions are for developers modifying the shared libraries in a Linux
bundle. They do not describe installing Reword, and they do not establish bit-for-bit
reproducibility. The currently audited bundle uses Debian 13 amd64 packages;
its minimum glibc is 2.39. Windows build material is still being completed.

## Choose the matching source package

Keep the installer, its `runtime-manifest.json` and the matching source bundle
together. `source-index.json` binds the exact bytes of that runtime manifest by
SHA256. Check this binding before using a source bundle for a different build.

The runtime manifest's `thirdParty.files` maps each shipped library/plugin/QML
file to a component. `thirdParty.components` gives its Debian binary package,
source package and source version. A source package can produce several binary
packages. Match the complete source version, including its epoch; do not choose a
newer release merely because the filename looks similar.

The source bundle contains the exact `.dsc` and every original/component/Debian
patch archive it names. The index records sizes and SHA256 values. Authentication
proofs identify signed Debian archive metadata or a maintainer signature under an
explicit trust root; they are distinct from a claim that the library is secure
or that a complete compiler/build environment is reproduced.

## Build in an isolated Debian environment

Use a Debian 13 amd64 development VM/container with enough disk space for the
source build and output packages. Some packages, especially GCC and Qt, require
substantially more space/time than their compressed archive size. Work as an
ordinary user. Prepare build dependencies inside that development environment;
Reword does not install compilers or packages on an end user's system.

For the chosen descriptor:

```sh
dpkg-source -x package_version.dsc work
cd work
dpkg-checkbuilddeps
# Satisfy the reported build dependencies in this development environment.
# Review debian/control, debian/rules and package-specific README files.
dpkg-buildpackage -b -us -uc
```

`dpkg-source` applies the Debian source/patch layout. Do not disable archive
checksum verification. The retained authentication evidence is needed for
historical signatures/keys that cannot be checked by a development environment's
current maintainer keyring. Source trees can be modified after extraction; keep
those modifications and their build configuration separate from the unmodified
published source materials.

`debian/control` and the package's build files describe its build dependencies
and options. This recipe preserves Debian's build approach; it does not claim
that a generic invocation reconstructs every original binary byte or that every
optional build profile has been exercised by this project.

## Use a replacement library

Quit Reword before changing the runtime. Work on a copy of the AppDir rather than
modifying an installed system directory. For an AppImage, extract it as your user
with `--appimage-extract`; for a `.deb`, use `dpkg-deb -x` into a new user-owned
directory. Neither extraction needs a system installation.

Rebuilt Debian packages can also be unpacked with `dpkg-deb -x` into a separate
directory. Copy the rebuilt shared object to the corresponding relative bundle
path from the runtime manifest. The bundle flattens shared library dependencies
under `usr/lib`; Qt plugins remain under `usr/plugins` and QML modules under
`usr/qml`. Keep matching SONAMEs and ABI-compatible builds. Qt libraries, plugins
and QML modules may need to be replaced together using a consistent Qt build.
Keep the rest of the test bundle intact so that dependencies remain available.

Run that extracted copy with `AppRun`. The application does not enforce its
runtime provenance manifest as a loader allowlist or reject a library solely
because the file bytes differ from the distributor's original inventory. That
inventory describes the distributed build. Packaging checks refuse changed
Debian bytes when producing an official package; they do not prevent a user from
running a modified extracted runtime.

Validate the replacement with the project's tests and controlled native fixtures
before typing into real applications. ABI mismatch, build options and plugin
compatibility can prevent loading. Replacing a library does not waive application
permission checks, password exclusion or guarded text replacement.

See [SOURCE_DISTRIBUTION.md](SOURCE_DISTRIBUTION.md) for the exact source assembly
command, authentication boundaries and current release gates. Distribution
terms remain in the original package copyright/license files and the library
source archives; the project's own LICENSE does not replace those terms.
