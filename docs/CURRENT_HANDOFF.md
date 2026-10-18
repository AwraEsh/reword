# Current handoff — 2026-10-18

The approved scope is in [REQUIREMENTS.md](REQUIREMENTS.md). The repository is
intentionally public at the user's later request. Do not change visibility back
to private. Keep runtime data, local captures, dependencies and build outputs out
of Git. No release has been accepted as production ready.

## Source and verification

- C++20/Qt Quick application, shared correction/store core, X11 helper, Windows
  adapter and Fcitx addon are implemented. See [architecture](ARCHITECTURE.md).
- Service change c02bf5d fixes completed words being missed during typing bursts
  and retry advancement after rejected writes. The current verification counts
  are recorded below.
- Wayland hardening a87ed97 adds canonical hotkeys, full-context code protection
  and confirmation before successful retry advancement. Repeatable GNOME/Fcitx
  probes pass on real Qt and supported GTK3 fields. See
  [compatibility evidence](COMPATIBILITY.md) for exact limits.
- Source at 03069a1 passed the full Linux and Windows workflow run
  [38913418616](https://github.com/AwraEsh/reword/actions/runs/38913418616).
  Linux includes three isolated native GTK/service cycles. Windows includes all
  installed-file hashes, native-platform launch without developer paths, complete
  shipped-file removal while preserving a user-added file, and the native
  field/password/hotkey probe.
- Downloaded development installer: `dist/windows-ci-03069a1/Reword-Setup-0.1.0.exe`.
  SHA256: 5a8b56e4758e3ee22255c59a5008bcab1ab05ca5860144e36de76aa34dccf31a.
  It is a CI artifact, not an accepted public release.
- Reviewed the seven pages with synthetic content in light/dark themes, including
  minimum logical size 880x620 at 150% scaling. The switch indicators now clearly
  distinguish checked state and keyboard focus. Populated lists, dialogs,
  keyboard-only interaction and Windows-rendered captures still need review.
- Frequency tables contain 93,959 English and 31,166 Persian ranks with pinned
  hashes and attribution. They are heuristic ranking data, not universal coverage.
- The detached two-hour idle run tests an earlier binary, identified by SHA256
  ca6939b84c15fdc79ae32af5b5cafaecd8e68d6e1f07f03931e230eea8e2729a.
  It completed 7,202 seconds with 1,430 samples: app maximum RSS 96,960 KiB,
  helper maximum RSS 25,072 KiB and app CPU time 118.9 seconds. Its report says
  passed and both process handles are now terminal. RSS alone does not establish
  total allocated memory or exclude swapping. This idle baseline does not replace
  an active workload test on the final packaged binary.

## Commands

```sh
scripts/env.sh cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
scripts/env.sh cmake --build build -j2
scripts/env.sh ctest --test-dir build --output-on-failure
/usr/bin/python3 scripts/test-native-wayland.py --fixture qt
/usr/bin/python3 scripts/test-native-wayland.py --fixture gtk
python3 scripts/test-native-linux.py
```

Build serially. Do not overlap builds in the same directory. Local rootless tools
and dependencies live in ignored `.deps`; CI uses its own packages. Native test
scripts name their required host tools in their help/docstrings.

## Required work still open

1. Audit selected-operation cancellation/staleness, settings/permission changes
   during operations and actual lock/unlock behavior. Wayland cursor, continuation
   and expiry now pass the native fixtures, with the documented 250ms Reset
   correlation limit still requiring broader real-application evidence.
2. Extend the Windows storage evidence to a normal Windows 10/11 desktop account;
   protected native NTFS DACLs and outside hard-link preservation now pass CI on
   the Windows Server 2022 runner.
3. Complete keyboard-only navigation, populated lists and dialogs, plus rendered
   Windows UI review. Native shortcut conflict recovery now passes Linux/Windows
   CI; ordinary Windows desktop interaction still needs acceptance.
4. Rebuild Linux AppImage/.deb from the final source; current local packages are
   older. Verify runtime closure, exact minimum glibc, host integration dependency
   declarations, bundled third-party notices and clean-environment launch.
   The distribution branch now derives the real glibc floor and rejects unresolved
   libraries; its real staged closure requires GLIBC_2.39 and the .deb declares
   >=2.39. Staged and extracted-.deb launches pass without developer paths.
   Final-source rebuilds remain open; the addon-dependencies branch now derives
   host package constraints with dpkg-shlibdeps and validates real staging. Supply corresponding copyleft-library source/notice material
   before public binary distribution; a third-party project URL alone is not an
   adequate completed source-delivery workflow.
5. Expand real compatibility tests to browsers and representative GTK/Qt/Windows
   editors, including focus races and fast typing. Supported synthetic fields do
   not prove broad application coverage.
6. Run a final multi-hour active/resource test and refresh latency measurements.
7. Produce matching versioned artifacts, checksums, repeatable install instructions
   and a requirement-by-requirement acceptance audit before declaring completion.

The source has no redistribution license yet; [LICENSE](../LICENSE) states the
current terms. Third-party licenses are recorded separately. Public visibility
does not grant a source license or prove installer acceptance.

## Storage integration and distribution work

Storage hardening was merged in [PR #1](https://github.com/AwraEsh/reword/pull/1),
merge commit 17e59b6. Source d3f00ba passed the full Linux/Windows run
[38951426522](https://github.com/AwraEsh/reword/actions/runs/38951426522), including
native ACLs, outside hard-link preservation, installer validation and native
Windows input probes. Five local suites passed after integration (12.58 seconds).
The Windows native security handle needs READ_CONTROL as well as write-security
rights; no privilege elevation is used. Earlier downloaded installers predate it.

Packaging was merged in [PR #2](https://github.com/AwraEsh/reword/pull/2),
merge d4b0bf0, after successful Linux/Windows run 38953164712. Six suites
pass on the primary checkout (13.13 seconds). The `linux-distribution` worktree
retains its development packages.
It adds eight isolated packaging regressions and a sixth CTest suite. The initial
regressions reproduced incorrect glibc metadata and accepted unresolved libraries;
follow-ups cover the addon and non-C locales. Independent review has no remaining
Critical/Important findings within this scope. The real staged package contains
240 ELF objects and declares glibc >=2.39. Staged and extracted-.deb smoke launches
pass with a clean environment and private data/D-Bus, without developer paths.
These are development artifacts, not an accepted public release. See
[package checks](LINUX_PACKAGING_CHECKS.md) for the remaining distribution gates.

## Shortcut recovery integration

[PR #3](https://github.com/AwraEsh/reword/pull/3) merged as a273876 after source
9915766 passed both platforms in [run 38957426101](https://github.com/AwraEsh/reword/actions/runs/38957426101)
and PR run 38957436135. All six suites pass after integration on the primary
checkout (15.78 seconds). Transactional rollback, concurrent/revised settings,
temporary storage failure and unavailable-registration recovery are covered by
Service regressions. Native X11 confirms restored Pause delivery in three private
cycles; Windows confirms it with SendInput on the Server 2022 runner. Fresh
private GNOME Wayland Qt/GTK3 probes also exit successfully. Read
[shortcut verification](SHORTCUTS.md) for scope and limits. Previously downloaded
installers and local distribution artifacts predate this integration.

## Wayland retry integration

[PR #4](https://github.com/AwraEsh/reword/pull/4) merged as bc4737b after source
7bd4d00 passed Linux/Windows runs 38961399942 and 38961425946 (attempt 2). It adds
surrounding/capability/Reset handling, actual Backspace rewriting, cursor and
continuation regressions, and 31-second expiry/Pause-after-idle checks. A no-op
Delete must not exempt a later cursor Reset. The native probes first reproduced
navigation, continuation, control-character and stale-exemption failures.

The stronger Pause assertion exposed an earlier false-positive Wayland test:
an uncorrected word could be an even retry, without Pause ever taking effect.
Fcitx reserves Ctrl+Alt+P for TogglePreedit. The new default Ctrl+Alt+Shift+P is
shared by Store, UI and addon; explicitly saved bindings are preserved. Native
X11 confirms delivery of the updated default in three cycles. General Fcitx
conflict reporting still needs audit. See [retry evidence](WAYLAND_RETRY.md).

The six-suite branch run passes in 15.46 seconds; the primary checkout after
integration passes all six suites in 16.33 seconds. Final native Qt and GTK processes both exit successfully, including Backspace,
no-op Delete/client cursor reset, continuation, expiry and Pause after idle.
Windows compilation, tests, installer and native probe all pass. The initial PR
Linux run timed out waiting for its private Openbox manager, before input probes;
its six suites had passed. The same-SHA push run passed, and rerunning only the
failed PR job passed. Startup timing remains an unresolved harness flake, not a
proven application defect or an explained fix. Do not
claim ordinary Windows desktop, all-client timing behavior or physical lock
acceptance from these fixtures. The deletion/Reset correlation has a documented
250ms bound. A targeted Qt probe now changes Password/Normal while a rejected
native correction is unconfirmed, then presents a late matching plaintext
snapshot: statistics/history stay unchanged and the next retry starts fresh.
The complete updated Qt run exits successfully. This exercises the combined
capability/Reset/timer protection paths, not exclusive callback dispatch.
The new regression merged in PR #5 as 69b206d after source 4df50e4 passed both
platforms in runs 39029896682 and 39029916494.


## Host-addon metadata integration

[PR #6](https://github.com/AwraEsh/reword/pull/6) merged as 7db0555 after source
3343102 passed Linux/Windows runs 39030487255 and 39030511674.
Fourteen packaging regressions pass; four new failures were observed before the
fix. Actual dpkg-shlibdeps inspection includes previously omitted libstdc++6
>=13.1 and Fcitx core/utils >=5.1.12. A private real staging run contains 240 ELF
objects, builds .deb metadata with libc6 >=2.39 and launches AppRun with no inherited
developer paths. Existing development packages were preserved; no new release is
published. Independent review has no Important finding. See
[package checks](LINUX_PACKAGING_CHECKS.md) for exact constraints and remaining
notices/source-delivery/final-installation gates. The primary packaging CTest
passes after integration (1.76 seconds).


## Original recovery integration

`codex/original-recovery` reuses the managed `shortcut-conflicts` worktree. It
implements bounded in-memory recovery for workspace/native Service passages,
independent of saved History, plus explicit Copy/Clear controls. Actual stale
workspace Undo with History off was RED before the Copy action/cache existed.
Native Service delivery, Unicode bounds, non-persistence and a 50k ceiling bypass
also have RED/GREEN evidence. Six local suites pass (17.86 seconds). Reviewed
Overview/History screenshots at 880x620 logical/150% scale are readable.

The second phase implements framed Fcitx delivery with account-authenticated
peers, bounded buffers/clients/deadlines and no disk mailbox. A live app handshake
now gates addon input; disconnect clears pending state. Actual GUI Quit initially
failed the native probe, then Quit/crash/restart pass with both Qt and GTK.
History-off passage delivery is confirmed on Qt; unsupported GTK selection stays
unchanged. Seven local suites pass (23.42 seconds). Review found an endpoint
impersonation risk and a destructor callback lifetime defect; both are fixed with
OS peer checks, signal suppression and callback guards. Independent final source
review has no Important finding. Final Qt/GTK reruns after authentication pass. Linux/Windows workflows
39033895148/39033897846 pass for dabeda4, including the separate foreign-UID
impostor probe with no skip. The final EOF/cleanup tightening (46b54e0) passes all four Linux/Windows jobs
in workflows 39034184071/39034186219. The foreign-account probe runs without
skips and requires connection EOF before greeting. PR #7 merged as b48cdc5;
primary develop is updated and clean. Current engine probe: 1,400 synthetic word calls, p95 19.67ms;
50k-character repeated passage 104ms. This is not end-to-end input latency. See [recovery evidence](ORIGINAL_RECOVERY.md).
No production claim or secure-memory-erasure claim is made by this integration.


## Next acceptance work

The next branch is `codex/distribution-notices` in the existing managed
`linux-distribution` worktree, based on b48cdc5. No release has been accepted.
Start with the binary-distribution gate: inventory every shipped dependency,
include its actual notices/license text, and provide matching corresponding
sources plus build/replacement instructions for the shipped libraries under
our distribution control. THIRD_PARTY.md links alone are incomplete. Preserve
the user's current project license choice; public visibility is not an
open-source license grant. Plan the source/archive size before fetching because
the current filesystem has about 2.4 GiB available. Do not remove user files.

Then rebuild final-source .exe/AppImage/.deb with matching source manifests and
checksums. Validate installation and clean-environment launch; extracted .deb
launch is not dpkg installation evidence. Wayland's Fcitx host setup remains an
explicit dependency. CI Windows Server 2022 evidence does not replace normal
Windows 10/11 desktop/UI testing.

Other acceptance remains: populated Applications/History/Insights and dialogs,
keyboard-only navigation, Windows-rendered UI; physical lock/unlock and focused
selection/settings/permission races; broader real browser/GTK/Qt/Windows editor
coverage; general Fcitx/compositor shortcut conflict reporting. The documented
250ms deletion/Reset compatibility bound remains. Run a multi-hour ACTIVE
workload on the final packaged binary, measuring helper CPU and private memory/
VmSwap as well as RSS. The old two-hour idle run is not that acceptance.


## Distribution notices branch

The provenance resolver and Qt deployment integration are implemented on
`codex/distribution-notices`. It checks each staged runtime file, includes verified
package/common notices and emits relative-path metadata. Registered GLX diversions
and equivalent multiarch copyright ownership have real-host evidence and tests.
The QML scanner now determines runtime modules; unneeded virtual-keyboard/SCXML/
timeline trees, designer files and legacy input providers are excluded. Required
transitive QML imports override optional uses; review caught this defect and RED
regressions cover both qmldir and runtime-QML cases.

47 functional checks and packaging CTest pass. Final temporary staging reports
524 runtime files, 134 components, 78 source identities, 197 ELFs and clean AppRun
exit 0. Existing installers are intact. Fresh review/CI and PR integration remain
pending. Then consume the runtime manifest's sourcePackage/sourceVersion pairs
for exact corresponding source archives and Windows materials. A license overview
or a source URL alone does not complete that distribution gate.


## Runtime notice integration and source assembly

[PR #8](https://github.com/AwraEsh/reword/pull/8) merged as a39913d from f365e5e.
Both platforms pass push run 39038063500 and PR run 39038124521. The primary
checkout passes all seven suites after integration (44.69 seconds), including
the updated 47-test packaging suite. The temporary real package audit records
524 runtime files / 134 binary components / 78 source identities / 21 QML modules;
clean AppRun launch exits 0. Final source delivery/installer acceptance remains open.

`codex/source-archives` reuses the managed linux-distribution worktree. The new
offline source assembler validates exact source versions and complete descriptor
checksum inventories, publishes atomically without replacing existing output, and
refuses symlinked file ancestors and special files at the actual open. Independent
review reproduced publication/open races; both have RED/GREEN regression coverage.
The branch's 68 packaging tests pass (9.84 seconds). Real Hunspell source archives
pass all descriptor digests, but descriptor authentication and all-source collection
are not complete. The exact-version snapshot size/availability inventory is complete: 78/78
identities, 440,968,366 archive/descriptor bytes. Transient DNS failures succeeded
on retry; historical filename aliases are retained. These metadata are not
authenticated source acceptance. See
[SOURCE_DISTRIBUTION.md](SOURCE_DISTRIBUTION.md) for commands, hashes and trust limits.
No release is published by this work. Windows materials, remaining desktop/UI
acceptance and multi-hour active stability remain required.


## Authenticated exact source descriptors

PR #9 merged as b985207 from430000b after Linux/Windows runs 39124936466 and
39124972344 passed. Primary integrated packaging CTest passes13.27s. The managed
linux-distribution worktree now uses codex/source-authentication. Source2ecb35a
adds optional required descriptor authentication with explicit local keyrings,
complete signed archive index hash chains and fail-closed exact identities.
The actual78-descriptor set verifies:75 archive-index proofs and 3 maintainer
signatures. Unknown/expired/revoked signatures and indexed mismatches are refused.
A historical signed proposed-updates index proves the exact at-spi2-core version;
no expired key exception is introduced. Source documentation records the trust
inputs, dates, hashes and limitations in SOURCE_DISTRIBUTION.md.

All 87 packaging checks pass 18.39s. After rebuilding the older worktree (initial
full CTest could not find recovery_tests), all 7 suites pass 39.26s. Independent
review has no Important/Critical finding and independently passes 19 auth tests.
Exact archive acquisition is running in the ignored source cache; descriptor
authentication alone is not archive delivery. No release has been published.
Source/publication/build instructions, Windows material and final desktop/active
stability acceptance remain open. Disk space changed externally from 715 MiB to
13 GiB; no cleanup was performed by this task. Recheck before large copying.


## Complete local dependency source material and remote upload

PR #10 merged as b69b1a3 from a3e842d after Linux/Windows runs 39127584465 and
39127671396 passed. Integrated primary packaging CTest passes 11.13s. The managed
linux-distribution worktree now uses codex/linux-source-materials. All 186 archives
and 78 descriptors are collected, independently assembled and authenticated.
The 264 source files total 440,968,366 bytes; the final tar.xz contains 284 regular
files with public verification evidence and rebuilding instructions. Its SHA256
is 94c96989f55d2bed81f37c5149a9d10cfc4762c970f2ee4fe31bb682df0a2df3.

GitHub draft release 416448800 targets b69b1a3 and tag
dependency-sources-debian13-2026-10-18. Large asset 653490023 remains uploading at
this checkpoint; checksum asset 653490027 is uploaded. Recheck live process/status
before any retry. Detailed active upload handles and paths are in the ignored
.deps/source-acquisition/NEXT_STEPS.md. The public receipt records observed states;
remote byte verification and public availability remain open. Do not claim the
draft material publicly delivered or the application ready for release.

Next: finish/verify this upload, complete Windows notices/source material, then
remaining keyboard/populated UI, native race/lock and active performance/stability
acceptance. Source archives are only one gate of the approved product scope.


## Operation policy boundary audit

A read-only review reproduced two Service policy races at b69b1a3. The operation-
policy branch adds 15 synthetic cases across live bursts, selected completion
and Undo: Pause, denial, language disable, Cancel and Cancel followed by prepare.
Initial live/selected tests were RED in all eight original cases; Undo was RED
for language/Cancel/cancel-reset after the initial read-access fix. The fix uses
fresh Access plus configuration and monotonic cancellation revisions before
native writes. Cancellation remains scoped to existing work, not future typing.

Fcitx now reads configuration revision before options and refreshes policy after
blocking lock inspection. The new optional command is:

```sh
/usr/bin/python3 scripts/test-native-wayland.py --fixture qt --policy-races
```

It owns a separate D-Bus/compositor session and synthetic screensaver service.
The exact baseline addon fails selected denial with `the` replacing `teh`;
the fixed addon passes selected Pause/denial and Undo Pause/denial. Undo's query
counter can include timer observation, so these native Undo cases are boundary
checks, not proof of hitting that precise internal call. Synthetic fixture state
is saved only inside this test's private temporary directory. Default probes
continue to use the compositor's ordinary lock service.

Independent final review found no remaining Important implementation defect.
Full CTest and default Qt/GTK native reruns, final CI, Git integration and exact
check results are recorded at the next checkpoint. This does not establish real
host lock/unlock or normal Windows 10/11 desktop acceptance.

Operation-policy verification at source 05e389d: all seven local CTest suites
pass in 35.16 seconds; both default Qt and GTK isolated Wayland probes pass.
The final optional policy probe passes all four selected/Undo Pause/denial cases.
The baseline-denial failure is retained in the ignored native-baseline log;
service RED/Undo RED logs and the final build/test logs remain under `.deps`.
Linux/Windows CI runs 39132778701 and 39132810221 pass at that source commit.
PR #12 carries the code and this evidence. PR #11 source-material documentation
merged separately as ba1815c after green runs 39132083252/39132227777.

The source archive upload remains live in its original gh process/session; the
remote archive state is still `starter` and public byte verification is pending.
Do not restart it solely because of a slow upload or a status-observation timeout.
Windows distribution inspection confirms CI uses Qt 6.8.3 and Hunspell 1.7.5;
the Debian source bundle cannot serve as their matching Windows source material.

## Windows build-material audit work

PR #12 merged as c5628aa after final-head Linux/Windows runs 39133319979 and
39133324256 passed; the primary checkout rebuild and all seven CTest suites pass
in 40.36 seconds. The former shortcut/distribution worktree directories were
removed during PR branch cleanup; their shared `.deps` logs and primary source
materials remain intact. Do not assume those old attached paths still exist.
The remaining storage-access worktree is reused for codex/windows-runtime-materials.

The new audit inventories every staged file and exact binary candidates under
explicit SDK/vcpkg/CRT roots; notice copies and metadata hashes live in a separate
CI artifact. It does not change deployed components or claim release readiness.
Review reproduced silently skipped unreadable directories; a RED regression now
requires traversal errors to stop publication. Local packaging checks and fresh
Windows execution/results are recorded in the next checkpoint.

Four Qt 6.8.3 source candidates (qtbase/declarative/svg/tools) are fully downloaded
and checksum-verified, without extraction. See WINDOWS_DISTRIBUTION.md for hashes
and unresolved source/notice/ABI binding. Acquisition session 27393 is terminal
exit 0. The original Linux archive upload is still live; preserve its handle and
verify actual server bytes before claiming delivery.

Windows audit source d742c62 passes Linux/Windows CI 39134648774/39134700661.
The second run's actual evidence was downloaded and inspected: 1,318 files,
96 binaries, all exact matches (Qt 84, VC 10, vcpkg 1, app 1), seven candidate
notices and 158 metadata hashes. The only Qt notice candidates are CMake helper
licenses; these do not establish runtime Qt coverage. CRT notices are absent
from the selected DLL directory. The report now makes the unresolved per-runtime
notice coverage explicit. Mesa/D3DCompiler/Mimer are staged and need resolution.
All 95 local packaging tests pass in 10.107s; independent review's traversal
finding is fixed and all eight audit tests pass. Full runtime/source/ABI/license
mapping and final Windows acceptance remain open. See WINDOWS_DISTRIBUTION.md.

## Source delivery verification and release gate — 2026-10-18

The earlier upload-in-progress checkpoints above are superseded. Archive asset
653490023 and checksum asset 653490027 are uploaded in Draft release 416448800.
Full remote byte streaming verified 481,747,380 archive bytes and SHA256
94c96989f55d2bed81f37c5149a9d10cfc4762c970f2ee4fe31bb682df0a2df3;
the checksum asset also matches. The verifier exited successfully; no upload or
readback is still running. See the public packaging/source-materials receipt
and SOURCE_DISTRIBUTION.md for scope. Preserve Draft status pending release gates.

The latest user instruction requires ordinary-user GitHub installation/use docs,
a stable simple installation, an actual release only after quality/stability is
established, and then installation of that accepted Linux build on this machine.
Do not treat green CI or successful source delivery as product acceptance. No
local development build was installed. Repository remains intentionally PUBLIC.
UI PR #14 is still under repair and verification; Windows source/runtime notices,
final packaged active soak and normal desktop acceptance remain open.

## Active stability harness: process sampler

The storage-access worktree now uses codex/active-stability-harness from c4e7fb3.
Task 1 of docs/superpowers/plans/2026-10-18-active-stability.md adds bounded,
content-free Linux /proc sampling for app/helper CPU, RSS, private/PSS/data/swap,
threads and descriptors. It brackets process identity and CPU reads and rejects
PID reuse, missing metrics and backwards counters/time. CPU percent uses one
logical CPU as 100%; child CPU is separate. It retains constant-space aggregates.

All 13 sampler tests pass, including real own-process sampling. Review reproduced
CPU rollback within a sample; the regression was RED and the fix independently
verified. Full build and all eight local CTest suites pass in 53.05s. CI and final
integration are separate. The actual packaged GUI workload, fixture watchdog/
timing extension and multi-hour acceptance run are NOT implemented by this task.
The earlier idle soak remains insufficient; no release or installation gate is
closed by merely adding a resource sampler.
