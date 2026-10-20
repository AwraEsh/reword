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
- `build-metadata/`: exact whitelisted Qt/vcpkg metadata under stable vendor-relative
  names, checked against the recorded bytes and SHA256.
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
Build metadata is retained separately with a 1 MiB per-file and 8 MiB total
limit. Only the explicit metadata filename whitelist is copied; altered copies
remove the owned partial audit. This evidence contains vendor build records,
not application runtime data.

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

## Bounded import dependency collection

The PE evidence work adds a read-only AMD64 PE32+ parser for ordinary and delayed
DLL imports. It bounds files to 128 MiB, sections to 96, descriptors to 2,048 per
table and DLL names to 260 bytes; malformed/truncated or ambiguous mappings fail
instead of becoming empty dependency lists. The implementation follows the
[Microsoft PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).

The packager requests collection against only its stage and the runner's explicit
Windows System32 directory. Parsed bytes must match the existing stage inventory.
The report maps DLL basenames to relative staged/system candidates and hashes;
different bytes under the same imported basename are ambiguous. Missing ordinary
or delayed DLL files return a failing audit exit status and stop installer
creation, while retaining `audit.json` for diagnosis.

API-set contracts are listed separately, without claiming host resolution.
Runtime LoadLibrary calls, exported-symbol availability, DLL search-order details
and Windows 10/11 minimum-OS compatibility are not established by file presence.
The current-runner system files are evidence for that runner only. Source/notice
binding and product release readiness remain false. Twenty-two tiny synthetic
reader/collector/CLI tests pass under a 256 MiB address-space/10-second CPU limit;
actual staged-file collection and complete hosted CI are still pending.

The first real collection attempt (run 39314256438) failed on a rejected import
basename. The reader now also accepts safe .drv basenames and checks them against
the explicit system directory: Windows documents its print spooler as
[Winspool.drv](https://learn.microsoft.com/en-us/windows/win32/printdocs/openprinter).
This is a supported PE filename, not permission to search other directories.
The actual rerun is still needed to identify any remaining package gaps.

Collection failure now retains the trusted materials inventory with
`collectionComplete: false`, `ordinaryClosureComplete: false` and a fixed failure
classification, without exception contents. Successful collection has
`collectionComplete: true`; this does not imply ordinary closure or release
readiness. Twenty-five bounded fixture/CLI tests pass, including malformed-PE
failed-evidence retention and .drv resolution.

Run 39315174646 at 0561148 collected all 96 staged PE images successfully.
It found no conflicting candidates but refused the package for `libpq.dll`
(sqldrivers/qsqlpsql.dll) and `mimapi64.dll` (sqldrivers/qsqlmimer.dll).
Store opens only QSQLITE. Deployment now retains the required qsqlite.dll and
removes other SQL driver DLLs; graphics and other plugin categories are retained.
The installed clean-PATH smoke check also requires a valid SQLite database
header, so removing unused clients cannot hide a broken local storage driver.
Fresh hosted verification is required before declaring this package closed.

The follow-up at 7284138 passes both Linux and Windows in runs 39315937141 and
39315944368. The actual 93-image report has 89 candidate names, no missing or
ambiguous ordinary imports, 16 explicitly unresolved API-set host contracts and
only sqldrivers/qsqlite.dll. Installed clean-PATH launch created a SQLite
database; install/hash/uninstall and native field/shortcut checks also passed.
PR #19 merged as 15e8ad5. Audit artifact 12218965126 contains audit.json SHA256
5cc59ae4ed660092f34f082290a0a226afe2ec29beac56bb95ac30061f612d49.
This closes the ordinary candidate-file gate for that runner/build only.
Dynamic/API-set/OS compatibility, source/notice binding and product release
acceptance remain open.

## Retained cached-package evidence — 2026-10-20

Full hosted push run 39399260940 and PR run 39399290932 pass at db871eb.
The actual artifact retains 158 metadata files totaling 186,979 bytes; every
copied file matches its reported size and SHA256. Twelve targeted audit fixtures
passed after four observed RED cases for missing retention, per-file/aggregate
limits and changed-input refusal. Independent review found no important issue.

The [bounded Hunspell receipt](evidence/windows-hunspell-cache-db871eb.json)
compares actual cached-package SPDX/ABI records with the acquired frozen recipe
and source archive:

- Patch, portfile.cmake and vcpkg.json SHA256 all match the recorded port inputs
  from vcpkg commit e456309491fd875487bf02b8a0dca76c1b1b00cc.
- Hunspell 1.7.5 source archive SHA512 matches the SPDX resource record.
- The staged hunspell-1.7-0.dll SHA256 matches the SPDX binary-file record.
- The ABI metadata SHA256 matches the cached binary package identifier.

These are recorded recipe/source/payload matches, stronger than assuming the
current checkout built the cache. They are not an independent reproducible
build. Qt third-party/graphics materials, VC runtime notices and final
installer/source pairing still need resolution.
The retained Qt qconfig.pri reports Qt 6.8.3 and SDK compiler 19.39.33520; the
application build toolchain is recorded separately. releaseReady remains false.

The [libiconv receipt](evidence/windows-libiconv-cache-db871eb.json) follows the
recorded Hunspell dependency: the acquired GNU libiconv 1.19 archive SHA512 and
all seven frozen vcpkg recipe inputs match the retained SPDX records. Its ABI
file hashes to the same package identifier named by Hunspell's ABI record.
The archive and recipe files remain ignored local source candidates. This is
record correspondence, not an independent rebuild, GNU signature verification,
proof of inclusion in the staged Hunspell DLL or completed source delivery.
