# Reword implementation ledger

Authority: the approved implementation plan in this conversation (2026-10-15).
This ledger is chronological; see [current handoff](CURRENT_HANDOFF.md) for the
latest status and remaining acceptance work.

- [ ] Native adapter feasibility and build toolchain.
- [ ] Safety policy, retry cycle, bounded offline correction, persistent data.
- [ ] Linux/Windows/Wayland adapters and guarded transactions.
- [ ] English Qt Quick UI, tray, settings, editor, history and insights.
- [ ] Tests, real integration probes, packaging and handoff.

Ruling: this is an empty, non-Git workspace; initialize develop in place, without a redundant worktree.
Ruling: no administrator installation; unpack Debian development packages in ignored .deps.
Ruling: no Windows host is available; Windows source/build recipe can be prepared, but runtime acceptance must remain pending.
Ruling: the current session is X11; Wayland runtime acceptance requires a real Wayland login and must remain pending until performed.

## 2026-10-16: integration hardening

- The source repository is PUBLIC at the user's subsequent explicit request; this supersedes the original private-only requirement.
- Core correction, SQLite storage, seven-page English UI, tray, consent and packaging code are implemented.
- Independent review found and prompted fixes for Windows target reacquisition, hotkey delivery, capped UTF-16 reads, protected-span rules, and addon dictionary installation.
- Linux AT-SPI now runs in a child helper process with bounded stdin/stdout JSON frames. It does not share the GUI's GLib loop. Pipe access is private to the parent/child; no network listener is created.
- All native writes revalidate the expected field/text. Unsupported fields remain untouched.
- Full-passage work runs asynchronously with one in-flight operation, cancellation and deadline. Fcitx selected-text correction also runs off its input loop.
- Fcitx commits are observed before history/counts are recorded; guarded Undo is implemented. Real Wayland acceptance remains pending.
- Local build passed. Core/UI suite passed five consecutive repeats after helper isolation.
- Real GTK single-line entry probe passed: metadata does not contain text; stale writes rejected; replacement confirmed; password field read blocked.
- GitHub CI at commit 07f575b failed: Windows COM header include order, Linux missing accessibility bus. Fixes are now implemented and need a fresh remote run.

Remaining acceptance: Windows build/installer CI, real Windows field/hotkey tests, real GNOME Wayland/Fcitx integration, packaged launch checks, extended stability and performance evidence. This is a development build, not production acceptance.

- End-to-end GTK service probe now verifies first/second/third correction, both Undo paths, pause, selected-text history and password exclusion. The probe waits for service initialization before creating its fixture.
- New regression tests cover Unicode half-space normalization, immediate cancellation, storage failure containment and mutual exclusion of full-passage work.
- Full-passage jobs share a user-local operation lock across the GUI service and Fcitx addon.
- Initial performance sample: 1,400 word calls, p95 17.15 ms; maximum 28.83 ms; 50,000-character synthetic passage 103 ms. This is a bounded synthetic sample, not a complete workload benchmark.
- A two-hour offscreen idle stability run started; its report and process handle remain local under .deps/soak. Do not claim it passed before the process finishes.
- Remote Linux CI passed at 86f2e9d. Windows progressed to a SetValue(BSTR) type error, now fixed for the next run.

- Windows and Linux CI both succeeded at eade977, including the Windows .exe installer artifact. Physical Windows application acceptance was not implied.
- Added independent punctuation categories and opt-in terminal-period / introductory-comma rules; all obey language and category switches.
- Added user-only local activation: opening Reword again raises the existing process instead of creating a second background instance. Activation tests passed.
- The first idle stability process ended before its requested two hours; marked interrupted, not passed. A detached replacement is running under .deps/soak-current with a binary SHA-256 in its report.
- Shared-desktop GTK tests can lose focus to other real applications. A failing run reported fixtureStillFocused=0. Controlled display tests are being prepared; earlier successful native probes remain valid for their tested conditions.


## 2026-10-16: offline ranking and UI privacy

- Added pinned, attributed wordfreq-derived frequency tables: 93,959 English and
  31,166 Persian entries. The tables rank Hunspell candidates; they do not promise
  coverage of every word. Source hashes and CC BY-SA attribution travel with the
  data. Generation uses developer tools only; application correction stays local.
- Added regression checks for broader spelling candidates, lowercase proper names,
  pause cancelling pending work, and saved markup never starting HTTP/HTTPS loads.
  User-provided labels now explicitly render plain text.
- Formatted C++ and QML sources for maintenance. Include order is preserved for the
  Windows COM headers.
- Synthetic frequency-engine sample during a parallel build: 1,400 calls, word
  p95 37.98 ms, maximum 71.83 ms; a 50,000-character passage completed in 273 ms.
  This is a workload sample, not a hardware-independent latency guarantee.
- A private headless GNOME Wayland session launched the actual Qt application with
  exit code 0. Fcitx correction in that session is still being tested.
- Windows CI at 5a4f4c2 passed, including the real Edit-control/password/hotkey
  probe and installer creation. Linux CI failed to observe the controlled GTK
  fixture focus; its accessibility activation environment is being corrected.
- The detached two-hour idle run remains live at the last check (runner 9896,
  application 9904). It tests an earlier binary whose hash is in its local report,
  and is not acceptance of later source changes.


## 2026-10-16: typing bursts and retry transactions

- Reproduced three failing service cases before the fix: a completed typo followed
  by the next partial word, two completed words in one poll, and alternation
  advancing after a rejected write. The same tests pass after the change.
- New service tests also cover offset changes, punctuation preservation, the
  first/second/third cycle, focus reset, stale Undo, protected context and refusing
  even to request password text. The native probes remain separate checks.
- Fresh full local build and all four CTest suites passed (14.70 seconds).
- CI's accessibility failure was traced to missing `at-spi2-core`, which owns the
  D-Bus activation service. Added that runtime package to the Linux CI setup;
  a fresh remote run is still required.
- A private GNOME Wayland session with actual keyboard injection and the explicit
  GTK Fcitx5 module corrected `teh ` to `the `. The module loaded, discovered the
  synthetic application and wrote its heartbeat. Broader Wayland acceptance is
  still pending; the first successful word is not a full acceptance result.


## 2026-10-16: real Wayland probes and independent review

- Added a repeatable private GNOME Wayland/Fcitx test using real virtual keyboard
  input, owned synthetic GTK/Qt fixtures and private storage. The remote-input
  D-Bus owner PID must match the compositor launched by the test.
- The real probe exposed canonical-key mismatch for Ctrl+Alt letter shortcuts;
  normalization on both sides fixed live Undo and Pause.
- Independent review found code-span protection and unconfirmed-write retry
  gaps in the Fcitx live path. Both were reproduced in real Qt input tests before
  fixing. Fcitx now shares full-context completed-word protection and commits a
  successful retry decision only after observing the expected edit.
- Qt probe passed rejected-write retry, first/second/third, live Undo, code-span
  protection, pause/resume, selected correction/history/Undo and password exclusion.
  The rejection fixture confirms that an actual IME event was refused.
- GTK3 probe passed its supported live cases. Selection remains unavailable in
  this input module, and no history is recorded for the unsupported operation.
- Local CTest suites passed again after the changes (18.27 seconds). Optional
  synthetic UI captures exercise all seven pages in light/dark modes.
- Linux CI now starts a private Openbox window manager because the adapter's
  focus scan requires active-window state, which bare Xvfb does not provide.
  Remote evidence for that updated harness is pending.
- Windows installer acceptance now installs per user into a path containing
  spaces, removes developer paths, loads the native Windows Qt platform, and
  tests uninstall preserving a user-added file. It is CI-only; execution pending.


- The detached baseline idle run completed 7,202.09 seconds and 1,430 samples;
  report state is passed and both runner/application processes exited. Maximum
  app RSS 96,960 KiB, helper RSS 25,072 KiB, app CPU 118.9 seconds. This describes
  the recorded older binary and resident memory only, not a final active-workload
  or total-memory acceptance result.
- Reviewed rendered synthetic captures of all seven pages in light/dark modes at
  1120x780. No overlapping content was observed in those captures. Switch contrast,
  smaller/scaled windows, populated lists, dialogs and Windows rendering still
  need closer review.


## 2026-10-16: settings visual review

- Replaced ambiguous gray switch indicators with clear checked-state accent,
  contrast in both themes and a keyboard focus ring. Changes are immediate.
- Replaced repeated generic page captions with descriptions of the page's task.
- Fresh build and all four local CTest suites passed (14.57 seconds). Captured
  every page at the 880x620 minimum logical size and 150% scale; the compact
  capture test passed. Reviewed overview, language and privacy captures without
  overlapping content. Scrollable settings continue below the viewport.


- CI at a87ed97 passed the entire Linux job, including three controlled native
  GTK/service cycles. Windows install, native-platform clean-PATH launch and the
  original uninstall assertions passed; the job then failed while deleting its
  test directory. The test had waited only for the uninstaller launcher / first
  file deletion. It now waits for the registry cleanup at the end of the uninstall
  section, verifies installed-file hashes and refuses leftover shipped files.
  Cleanup failure no longer hides the original acceptance result. Fresh Windows
  execution of these stronger assertions is required.


## 2026-10-16: installer and controlled desktop gates pass

- Workflow run [38913418616](https://github.com/AwraEsh/reword/actions/runs/38913418616)
  at 03069a1 passed both Linux and Windows jobs. The Windows test verifies the
  hashes of every staged/installed file, native Qt startup with a clean PATH,
  complete uninstall of shipped files and preservation of the added user file.
  The subsequent native Windows field/password/hotkey probe also passed.
- Downloaded the matching Windows development installer locally. Its SHA256 is
  5a8b56e4758e3ee22255c59a5008bcab1ab05ca5860144e36de76aa34dccf31a.
- Reviewed all seven compact-page captures at 150% scale; captions, controls and
  mixed Persian/English workspace text remain readable without overlapping content
  in the captured states. This does not replace populated/dialog/keyboard testing.
- Preliminary packaging audit: the old Linux dependency closure requires
  GLIBC_2.39 through libsystemd. The hardcoded >=2.38 dependency must be corrected
  from measured final-bundle requirements. Copyleft source delivery and full
  bundled notices remain release work; see
  [Qt's source-delivery guidance](https://www.qt.io/development/download-open-source).

## Storage access hardening — 2026-10-16

Work continues on `codex/storage-permissions` in the managed `storage-access`
worktree. New regression tests first reproduced two Linux failures: a registered
SQL connection survived a failed Store constructor, and an existing SQLite
sidecar retained mode 0644. The constructor now releases its connection on all
initialization exceptions. Storage permissions are checked before SQLite opens
existing database/sidecar paths, and again after a new database is created.

On Linux the helper opens paths without following their final symlink, checks
current-account ownership and regular-file/directory type, and requires chmod
0700/0600 to succeed. On Windows it opens the native object without following
reparse points and installs a protected DACL with explicit full access for the
current account and SYSTEM; directory entries protect newly created children.
Failure stops storage initialization with a fixed message containing no path or
user text. Storage remains unencrypted.

Local Release build succeeded and all five CTest suites passed (16.56 seconds),
including storage persistence and unchanged synthetic sidecar content. The
Windows implementation still requires fresh Windows CI evidence. This change
does not claim protection against a malicious process already running as the
same account. No production release is accepted.

Independent storage review found ancestor-link traversal and hard-linked files
were still accepted. Two added POSIX regressions reproduced both defects before
the follow-up: the linked ancestor allowed creating a storage directory at the
external target, and rejecting an invalid hard-linked database still changed its
external alias from 0644 to 0600. The implementation now rejects linked ancestors
before creating a directory and requires one link for database/sidecar files
before changing access. Windows uses native reparse attributes and link counts;
its link-fixture coverage remains separate from the POSIX-only tests.

CI run 38948425106 passed Linux (including native GTK probes), but Windows stopped
compiling the test because Qt MOC saw a POSIX-only slot omitted by the C++
compiler. Slot declarations are now unconditional, with platform-specific test
bodies. Fresh CI is required; the first run is not Windows acceptance. The local
native X11 probe could not start because Xvfb is absent from the current host's
PATH/tool directory; this is recorded separately from the passing CI probe.

Follow-up review also caught a `link/..` normalization mismatch and Windows
directory ACL propagation preceding file validation. A regression reproduced
the parent-traversal mismatch on Linux; parent traversal is now rejected before
normalization. All existing SQLite files are preflighted without modifying their
permissions before directory ACL hardening. A native Windows hard-link fixture
compares the external alias DACL and content before/after rejection. This Windows
fixture has not yet run successfully; the follow-up requires CI evidence. All
five local suites passed after implementation (12.82 seconds); platform-specific
link tests explicitly skip on the other OS.

CI for 6c24b15 (38949857941) compiled Windows successfully but all storage-using
tests failed during initialization. Linux, including native GTK probes, passed.
A guarded initialization test now reports only fixed failure-stage text and an
optional numeric Win32 error. The Windows workflow inspects its synthetic test
temporary-directory ancestors to distinguish a redirected path from native
permission API failure. Security protections are not weakened to make CI pass.

A fresh private GNOME 48.7 probe on this source failed while creating its
RemoteDesktop session (the three-second D-Bus call timed out). The process is
terminal; no correction assertion ran. This is not counted as current Wayland
acceptance and does not supersede earlier recorded successful probes.

The Windows diagnostic invocation reported ordinary non-reparse temporary-path
ancestors, but no QtTest result was captured before its failure. It now uses
Start-Process with explicit waiting and a QtTest file logger, and checks the
actual process ExitCode. This avoids treating missing GUI-process stdout or an
unset PowerShell LASTEXITCODE as evidence of a native permission failure.

Increasing only the private GNOME CreateSession startup-call timeout from three
to fifteen seconds made the checked-in Qt fixture probe pass all assertions on
the current storage binary. Other D-Bus calls retain their three-second limit.
The private compositor PID/owner checks and isolated directories are unchanged.

The awaited Windows diagnostic now identifies the failing native call:
SetSecurityInfo returns ERROR_ACCESS_DENIED (5) on a newly created private
directory; the ordinary temporary ancestors are not reparse points. The handle
previously requested write-security and attribute rights but omitted READ_CONTROL.
The next CI tests adding security-descriptor read access to the existing native
handle request. This is a targeted hypothesis, not a verified fix yet, and no
privilege elevation or relaxed ACL is introduced.

The checked-in private GNOME/Fcitx probes now pass on both Qt and supported GTK3
fields after the bounded startup-timeout adjustment. Qt additionally verifies
selection correction/history/Undo and rejected-write retry preservation; GTK3
still refuses unsupported native selection. Both processes completed normally.

## Storage integration and package audit — 2026-10-16

Adding READ_CONTROL to the Windows security handle resolved the diagnosed
SetSecurityInfo access-denied failure. Source d3f00ba passed both platforms in
run 38951426522, including protected ACL/alias regressions, installer launch and
uninstall, and Windows native input. PR #1 merged as 17e59b6. Five suites pass on
the primary checkout after integration (12.58 seconds). Both private Qt/GTK
Wayland probes also pass with the bounded GNOME startup call; unsupported GTK3
selection remains refused.

The separate linux-distribution branch adds functional package-command regressions.
Initially, a synthetic unresolved library still produced a .deb and a synthetic
GLIBC_2.39 requirement still declared >=2.38. Both failures were observed before
the implementation. Follow-up review exposed missing addon dependency validation;
that and locale-dependent ldd parsing were also reproduced before their fixes.
Eight regressions pass and CTest now registers a sixth packaging suite. Native
addon dependencies are checked separately from the bundled GUI closure. A
runtime manifest records the derived glibc floor and library names without
machine paths. Real staging finds 240 ELF objects, requires glibc 2.39 and produces
matching Debian metadata. Staged and extracted-.deb smoke launches succeed with
clean environment/private D-Bus and synthetic data. Final release installation,
source/notice delivery and final-source rebuild acceptance remain open.

## Shortcut conflict recovery — 2026-10-16

Packaging PR #2 passed both platforms (run 38953164712) and merged as d4b0bf0.
Six suites pass on the primary checkout after integration (13.13 seconds).

The shortcut-conflicts branch reproduces native registration failures losing the
old bindings while retaining rejected settings. Service now restores its last
working registrations and uses transactional tuple/revision comparison before
reverting persisted settings. Newer edits survive. Follow-up regressions exposed
a permanently cached failure after a SQLite write error, an X-to-Y-to-X revision
race and failure to retry unchanged unavailable bindings after re-saving. All
were observed failing before their fixes. The latest full six-suite run passes;
see SHORTCUTS.md for the cases and native verification scope.

Native X11 conflict probes also verify restored Pause delivery with XTest on a
private display. Three cycles complete successfully using ignored rootless test
tools. The first assertion-complete run exposed private GVFS cleanup failure;
the test directory now outlives its D-Bus session, with bounded own-mount cleanup.
The analogous native Windows conflict/SendInput probe passes fresh Windows
Server 2022 CI on source 9915766 (runs 38957426101 and 38957436135). PR #3 merged
as a273876. Both private GNOME Wayland Qt/GTK3 probes exit successfully on the
same source. This is CI/fixture evidence, not ordinary Windows 10/11 desktop or
broad application acceptance. Existing packaged artifacts predate this change.

## Wayland retry event flow — 2026-10-16

The `codex/wayland-retry` branch adds surrounding/capability/Reset observation and
predictive printable continuation for clients that defer surrounding updates.
Actual Backspace/Delete tests exposed control-character and stale-deletion
exemption defects. Reset ordering prevents late confirmation from reinstalling
an obsolete retry cycle. Native protocol evidence and remaining timing limits
are in WAYLAND_RETRY.md.

The default Pause combination is Ctrl+Alt+Shift+P, avoiding Fcitx's embedded
preedit hotkey. Explicitly saved settings survive. The old Wayland Pause test
was ambiguous with even retry attempts; the probe now checks persisted Pause
state and real behavior before and after a 31-second interval. Six local suites
pass (15.46 seconds), and native X11's three cycles confirm the new default.
Fresh Windows CI is required before integration.


PR #4 merged as bc4737b after Linux/Windows CI. The first PR Linux run timed out
before native input while waiting for Openbox; a same-source push run and the
failed-job rerun pass. Its cause remains unproven. All six suites pass after
integration on the primary checkout (16.33 seconds). The follow-up protected
transition probe passes on Qt: rejected/unconfirmed correction, Password/Normal
change, late matching snapshot, unchanged statistics/history and fresh retry.
This tests the combined real capability/Reset/timer path.


## Host-addon Debian metadata — 2026-10-17

The addon-dependencies branch replaces hardcoded host versions with a strict
actual-addon dpkg-shlibdeps audit. It preserves the stronger ELF/Debian libc
minimum and records host package constraints in the runtime manifest. Fourteen
whole-command tests pass, with real staging and clean-environment launch evidence
in LINUX_PACKAGING_CHECKS.md. This fixes omitted libstdc++ and Fcitx library
requirements as well as the incorrect fixed Qt version. Binary release notices,
source delivery and final install acceptance remain open.


## Session recovery draft — 2026-10-17

Original-copy access after stale Undo now works in the workspace and shared
native Service path with History off. One bounded pair stays in memory; Copy is
explicit and Clear resets the recovery/explicit workspace Undo buffers. Service
emits the confirmed passage before optional History persistence. The hard
operation ceiling is now 50,000 in Engine/Store/Settings; tests reproduced the
old 100,000 bypass. The bounded Fcitx local channel now forwards confirmed passages and gates input
on a live app connection. Quit/crash stops addon processing; reconnect resumes
only after a fresh authenticated handshake. OS peer-account checks prevent
cross-account endpoint impersonation. Seven suites pass locally (23.42 seconds);
Qt/GTK native lifecycle probes also pass after authentication. Linux/Windows CI and the foreign-UID regression pass for dabeda4 in runs
39033895148/39033897846. The final EOF/cleanup tightening (46b54e0) passes both platforms in
39034184071/39034186219. PR #7 merged as b48cdc5. The next distribution worktree
branch is codex/distribution-notices; binary notice/source delivery remains open. Read ORIGINAL_RECOVERY.md for protocol and limits. PR #6 packaging metadata has
merged as 7db0555; primary packaging CTest passes (1.76 seconds).


## Distribution provenance — 2026-10-17

`codex/distribution-notices` verifies copied runtime bytes against exact Debian
binary/source metadata, writes relative-path SHA256 manifests and includes the
actual package/common license texts. QML deployment now uses the build SDK's
scanner and excludes unused nested modules/designer files and unrelated input
providers. Real host GLX diversions and shared multiarch notices have regression
coverage. Review found a required QML import hidden by another optional import;
the RED cases now pass with required transitive imports overriding optional use.

47 functional checks pass, CTest packaging passes, and a real temporary package
with 524 runtime files/134 components/197 ELFs launches cleanly. No prior installer
was replaced. Corresponding source archives, Windows notices and final platform/
installation/stability acceptance remain open. See LINUX_PACKAGING_CHECKS.md.
