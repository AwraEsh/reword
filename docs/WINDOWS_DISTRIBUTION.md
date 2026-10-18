# Windows distribution evidence

Reword's Windows installer bundles Qt, Hunspell and app-local VC runtime files.
Users should not need a developer SDK or a separate Qt installation. The current
CI builds an NSIS per-user `.exe`; ordinary Windows 10/11 desktop acceptance is
still required before declaring it ready for general use.

## Build-material audit

`scripts/package-windows.ps1` runs a read-only audit after deployment. It supplies
the actual Qt root, installed vcpkg tree, selected VC143 CRT directory and original
application executable. The separate `Reword-Windows-Materials-Audit` CI artifact
contains:

- `audit.json`: size/SHA256 of every staged file, exact vendor byte matches for
  DLLs/executables, unmatched binaries, missing vendor notices and metadata hashes.
- `notices/`: available vendor license/copyright files with their original relative
  paths under stable vendor identifiers.
- `vendor-versions.json`: queried Qt version, installed vcpkg package listing and
  the checkout commit observed during the build.

Matching uses bytes. A shared filename or file-version string does not establish
provenance. All byte-identical candidates are retained when several SDK files
match. A patched deployment file remains unmatched unless a separate derivation
is established. Qt documents that deployment can patch QtCore's paths in some
configurations; this audit does not infer that every mismatch is such a patch.
See [Qt's deployment documentation](https://doc.qt.io/qt-6.8/windows-deployment.html).

Unreadable directories, links, unusual files and over-limit notices stop the
audit. The caller must own and keep the build trees unchanged during collection.
Notice files are limited to 4 MiB each and 32 MiB in total; copied notice bytes
are rechecked against the inventory. An existing evidence directory is refused.
Build metadata is hashed rather than copied verbatim into public evidence.

The audit is separate from the installer payload. Its `releaseReady` field stays
false: inventory is not source authentication or a completed redistribution
review. A vcpkg checkout commit does not, by itself, prove that cached installed
packages were built from that checkout. The audit records that distinction.

## Current inputs and remaining material

CI at operation-policy source 05e389d uses Qt 6.8.3, Hunspell 1.7.5 and MSVC
19.44.35229. Its Windows Server 2022 installation/native-field checks pass, but
that is not normal Windows 10/11 desktop evidence.

The official Qt SDK metadata identifies the MSVC 2022 package version
`6.8.3-0-202503201308`. Four version-matched source candidates have been downloaded
and verified locally against checksums served by Qt:

| Source archive | SHA256 |
| --- | --- |
| qtbase-everywhere-src-6.8.3.zip | 992bf7766e214a341ef793eb3665fb784787d2fd666955f5f507f4c6f1f770dd |
| qtdeclarative-everywhere-src-6.8.3.zip | 3ba85e9807958bd61606ce72af189be312396030b9946ca79d9e38347306d88e |
| qtsvg-everywhere-src-6.8.3.zip | 90643c1ba05245abedc2bcd9d4f745568dc27a1b37dd3ba861fd19d8ac6a7c46 |
| qttools-everywhere-src-6.8.3.zip | 4797251aa4d04c9d9da279d8da75f6348f76cff2a75192eac7cca81f5e1b1b38 |

These are candidates for the matching source set, not a complete binding to every
deployed SDK binary. They are not the Debian source bundle. Source archives and
their `.zip.sha256` files are under Qt's official
[6.8.3 submodule archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/).
The source acquisition harness and receipts stay in ignored `.deps`; no archive
has been extracted or executed by that collection step.

Before public binary release, inspect the real audit; bind exact Hunspell/vcpkg
source, patches and cached ABI metadata; complete Qt/third-party and VC runtime
notice material; and supply rebuilding/replacement instructions. The SDK package
also includes separate Mesa/D3DCompiler archives, so actual deployed files must
be inspected before deciding whether those components are present or removable.
Do not remove graphics support merely to simplify the inventory. Microsoft
explains its applicable runtime redistribution material in
[Redistributing Visual C++ files](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

Final installer/source pairing, checksums, clean launch, populated UI/keyboard
review and active stability acceptance remain explicit release gates.

## First real audit: d742c62

Windows CI runs 39134648774 and 39134700661 pass, including audit execution,
per-user install/clean-PATH launch/uninstall and the native field/shortcut probe.
The latter run's evidence artifact was downloaded and inspected locally:

- 1,318 staged files; 96 binaries, all with exact byte matches: application 1,
  Qt SDK 84, vcpkg 1 and VC runtime 10.
- No unmatched binaries. This is exact SDK byte provenance, not a complete PE
  dependency-closure or source/build correspondence proof.
- Seven available notice candidates and 158 metadata hashes. VC-runtime has no
  notice file in the supplied CRT directory. The two Qt notice candidates are
  CMake helper notices, so they do not establish runtime Qt license coverage.
- Mesa `opengl32sw.dll`, D3DCompiler47 and the Mimer SQL plugin are actually
  staged. Their material must be resolved or their removal tested explicitly.

`missingVendorNotices` means a vendor scan found no notice candidate at all.
A vendor absent from that list does not imply complete notice coverage. The
report separately states that runtime notice coverage is not established.
The current vcpkg checkout is e456309491fd875487bf02b8a0dca76c1b1b00cc;
its installed inventory includes Hunspell 1.7.5 and libiconv 1.19, but only the
Hunspell DLL is copied from that tree. Imported-DLL closure still needs inspection.
