# Current handoff — 2026-10-17

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


## Original recovery draft

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
review has no Important finding. Final Qt/GTK reruns after authentication pass. Fresh Windows CI and a separate
foreign-UID impostor probe in the disposable Linux runner remain pending, so
PR #7 stays draft. See [recovery evidence](ORIGINAL_RECOVERY.md).
No production claim or secure-memory-erasure claim is made by this draft.
