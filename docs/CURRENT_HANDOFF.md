# Current handoff — 2026-10-22

The user explicitly resumed work on 2026-10-21. The full product goal is active.
After two unexplained host freezes, local C++/Qt work has resumed only as serial,
resource-bounded jobs using existing dependencies. This is evidence for the tested
source, not general host-stability acceptance. Requirements remain in
[REQUIREMENTS.md](REQUIREMENTS.md); the repository is intentionally public. No
production release or local installation has been accepted.

## Current resumed work

- **Retry reset isolated, 2026-10-23:** owned numeric tracing reproduces live-2
  mismatch at 95.006 s. First correction 3218239 ms, same-text caret reset
  3218299 ms, unexpected first correction 3218510 ms. A deterministic split-read
  deletion regression fails before the fix (`the ` instead of `teh `). Service
  now confirms a caret-only observation through the same target/privacy guards
  before resetting; a persistent caret move still resets. Build passes and
  Service passes including both new controls. Full CTest: 14/15 pass, 115.90 s;
  fcitx_state timerCancelsPendingJobAfterPolicyChange(paused) fails witnessTicks
  >= 2 (observed 0 after 211 ms). Focused private-D-Bus timer control then passes
  all 5 cases, but the initial failure remains recorded. Do not call the whole
  suite green. Independent review finds no actionable safety/correctness defect;
  native split-read causality remains a modeled hypothesis, not proven atomic
  GTK evidence. Post-fix owned workload completes 12 first/second/third/Undo
  sequences and 121 selected corrections without text mismatch, then stops at
  resource-budget after 68.993 s (app CPU 10.070% across 32.17 s baseline,
  helper 5.197%, zero swap, private growth 340 KiB, clean shutdown). Receipt:
  .deps/retry-confirm-active-120/report.json. This is not a 120-second pass.
  See RETRY_DIAGNOSTICS.md for fixed numeric event codes and prior separate
  post-write/Undo failure. Installed Preview remains unchanged.
- **Important live retry reproduction:** hidden-history workload terminates at
  50.803 s with native-text-mismatch in live-2, not CPU budget. Second-attempt
  observation is corrected (five change records) instead of original. Five full
  cycles; live-first 7, second/third/Undo 6; 115 selected corrections. This
  reproduces the earlier long-run second-attempt failure with bounded synthetic
  diagnostics. App CPU 12.75% across only 16.08 s baseline (not comparable to a
  completed 30 s CPU gate); zero swap, private growth -76 KiB, word p95 upper
  bound 39.7 ms/13 samples. No forced cleanup or FUSE exception.
  Report .deps/usable-local-active-120-hidden-history/report.json.
  Do not replace the installed Preview or claim stability acceptance from this
  run. Prioritize tracing Service retry reset boundaries (same-text caret
  changes, invalid fields and post-write verification) using fixed reason codes
  and metadata only; no text capture or assertion weakening. UI optimization
  changes no native typing logic, so causality is not established.
- **Hidden History regression in progress:** the real QML test observes four
  history getter calls per update while Overview is shown (expected zero).
  A plain var binding still retains Qt's reference-backed sequence and causes
  five reads per one update on the open History page; do not accept that fix.
  Working revision uses explicit loadHistory() returning a value list, refreshes
  it on page changes/updates only while History is selected, and shares it among
  the four bindings. Test checks hidden reads, fresh insert visibility and
  per-update counts. Focused regression passes (3 cases incl setup/cleanup,
  zero failures/skips, 812 ms) and the complete UI CTest target passes.
  Full application relink succeeds and all 14 CTest targets pass in 92.25 s at
  2f8a780. Follow-up independent review is running. Identical bounded 120-second
  workload is active against .deps/hidden-history-AppDir with rebuilt binaries
  and unchanged audited runtime. App SHA256
  f1abc493441b41dbd1721c791b1077c11763e5ebfa729e1c7d5010839dc4fcc0;
  receipt .deps/usable-local-active-120-hidden-history. Installed Preview is
  unchanged until the new workload result is known.
- **Settings workload result:** the 120-second comparison stops at 65.933 s on
  the same unchanged resource budget. App CPU 14.007%, helper 6.336%, zero swap;
  nine full cycles, 11 live first/second/third/Undo sequences, 120 selected
  corrections/history inserts. Word p95 upper bound 34.2 ms across 22 samples.
  Work completed faster than the preceding six-cycle partial run, but whole-app
  CPU did not meet the 10% target. No broad CPU reduction or full stability pass
  is claimed. Outer cleanup now completes without the previous FUSE exception.
  Next investigate repeated hidden-page list bindings (four History reads per
  generic updated signal) before another package/workload run. Host session is
  confirmed X11; Fcitx is installed, but ordinary Wayland acceptance is separate.
- **Settings-read CPU optimization, 2026-10-23:** profiling 1000 Store::options
  calls with synthetic settings took 775.477 ms CPU before and 77.196 ms after
  replacing 25 separate queries with one fixed-key SELECT per call. No cache
  survives a call; other-connection policy edits are immediately visible.
  Added a cross-connection regression for language changes and numeric ceilings;
  complete core suite passes in 0.91 s. --settings-read-profile is available on
  reword_performance_probe for repeatable comparisons. This is a roughly 90%
  reduction for that isolated path, not a measured 90% whole-app reduction.
  Full relink succeeds; all 14 CTest targets pass in 101.55 s at 3e5193c.
  Independent static review finds no actionable defect and confirms behavior
  preservation and fresh cross-connection reads. An identical 120-second active
  workload is running against a private clone of the audited AppDir with only
  the rebuilt app/helper installed; runtime dependencies are unchanged. New app
  SHA256 2057c227d11f5eef5d4135a1ef7d953d353f18697bb7ebfbabdc6b629bb3c5a0;
  helper remains fb267a24a439c1782328b8c33232b55c9a9c61101740e6322d36efe55f908b97.
  Receipt directory .deps/usable-local-active-120-settings-snapshot. Final
  package refresh remains pending. Installed Preview is the previous binary.
- **Test cleanup fix, 2026-10-23:** test-only owned_mounts.py reads bounded
  /proc/self/mountinfo rather than stat/ismount, so a disconnected private FUSE
  endpoint is still recognized. Only runtime/doc and runtime/gvfs beneath the
  caller-owned test directory may be detached. Shared by X11/Wayland harnesses.
  Six unit tests pass (mount escaping, exact scope, unavailable tool, malformed
  or oversized inventory); all 18 existing active-stability unit tests pass.
  Actual controlled-font private-X11 fixture preflight exits 0 with the fix.
  The old disconnected mount was no longer present for direct reproduction;
  the ENOTCONN path is covered with a simulated kernel inventory. This changes
  no installed application code and does not resolve the app CPU budget failure.
- **Preview delivery accepted, 2026-10-22:** user asked to accept the current
  result and reach a usable outcome quickly. Installed the tested AppImage for
  this account at /home/debian/.local/opt/reword/reword-0.1.0-preview.AppImage,
  SHA256 e2e364a545fe00bf9b234441c6e1c99a622a543bb98bd304dd711a043a43d434.
  Reword Preview desktop launcher validates and uses extract-and-run. No
  autostart or system-wide package changes. Installed binary launches on the
  host display with temporary data/private D-Bus and exits 0 for --smoke-test.
  Windows installer artifact 12415994422 from integrated CI 39757127469 is linked
  in docs/TRY_REWORD.md; no stable release is claimed.
  Local packaged active test reached 73.685 s, then failed resource-budget:
  app CPU 14.18%, helper 4.11%, zero swap; eight live first/second/third/Undo
  sequences, 117 selected corrections/history inserts, six full cycles.
  Word p95 upper bound 33.2 ms, 16 samples. No forced process cleanup in the
  workload, but outer temporary-directory cleanup hit a stale portal FUSE mount
  (ENOTCONN). Both CPU and outer cleanup remain follow-up work; no thresholds
  were relaxed. Report: .deps/usable-local-active-120-font-control/report.json.
- **Usable-build integration, 2026-10-22:** the user resumed after pausing and
  asked to prioritize a usable, stable build for Windows and this Debian host.
  Current branch codex/usable-build-integration contains PR29 plus the separately
  verified populated-list UI branch (PR22) and workload diagnostics (PR24), merged
  without conflicts. Complete serial bounded build succeeds; all 13 CTest
  targets pass in 119.78 seconds at 672650b. Local .deb/AppImage staging succeeds
  with runtime provenance and host-addon dependency auditing. The initial
  dpkg-deb compression exceeded the job memory cap; DPKG_DEB_THREADS_MAX=1
  succeeds within the same bounds. Artifacts are about 40/50 MiB. AppDir,
  extracted .deb and AppImage extract-and-run each exit 0 for --smoke-test with
  empty inherited environment, temporary data and private D-Bus/software rendering.
  This is not dpkg installation, physical Tray or ordinary field acceptance.
  SHA256 .deb: 5d27530277db758ca03d8f58816a24b8b584a78a541bd09aa1418e1890e8182e;
  AppImage: e2e364a545fe00bf9b234441c6e1c99a622a543bb98bd304dd711a043a43d434.
  Draft PR30 contains this integration; integrated CI 39757127469 at de6ac2e
  is confirmed successful on Linux and Windows. No fresh build-tool download
  was needed. The bounded local 120-second owned-X11 attempt did not reach the
  workload. Initial startup lacked rootless tool PATH; adding it exposed missing
  Openbox resource lookup. Adding native-x11/root/usr/share to XDG_DATA_DIRS
  starts the desktop, but fixture_timing_probe fails its existing 10-second
  synthetic watchdog (child alive, one stream). Logs also show private-session
  GNOME screensaver activation/JIT failures under the address-space cap.
  Root cause of fixture startup remains unproven; do not count this as packaged
  typing acceptance or relax the fixture assertion. Receipt:
  .deps/usable-local-active-120-datadir.log. No user application was targeted.
  Follow-up: removing only RLIMIT_AS (retaining aggregate 1200 MiB/no-swap/CPU
  caps) removes the private-session JIT error but fixture watchdog still fails.
  Therefore virtual-address exhaustion does not explain the fixture timeout.
  A connect/poll/futex-only strace reproduces it and shows the owned GTK fixture
  in repeated 25 ms main-loop polls until the probe terminates it, not simply
  blocked in process startup. Build fixture strings include lifetime/timing
  options. Next inspect its synthetic stdin/timer dispatch before changing any
  watchdog duration. Ignored receipts: usable-fixture-no-address-cap.log and
  usable-fixture-startup.trace under .deps.
  Stack-unwound trace then locates the fixture at Pango font-map initialization
  during GTK control construction (main+0x188), before watchdog installation;
  the earlier poll-only inference of normal main-loop entry was too broad.
  A test-only FONTCONFIG_FILE restricting fonts to system DejaVu makes the same
  timing probe pass, including watchdog, two records and quiet mode. Host font
  configuration is unchanged. The exact reason the default font scan exceeds
  the bound is not yet established. A bounded 120-second packaged workload now
  runs with this controlled font configuration; report directory:
  .deps/usable-local-active-120-font-control. No production source change.
  PR29 CI 39752523899 at 6a6e11c is confirmed successful on both OS: Windows
  installer creation, install/launch/uninstall and native probe all pass; Linux
  CTest, UI and controlled GTK probes pass. No binary release/install is claimed.
  Next: verify integrated tests, produce and audit local Debian/AppDir artifacts,
  run owned package/native checks, and reconcile the remaining long-run and
  distribution acceptance gates before a stable release. Avoid opening unrelated
  feature work while these delivery gates are outstanding.
- **Fcitx operation UI:** current storage-access branch is
  codex/fcitx-operation-ui, stacked on PR28 at 512c0d4. Initial lifecycle/Cancel
  implementation fd31520 passed 12 CTest targets but independent review found
  two cancellation races. Real socket REDs reproduce coalesced cancellation
  loss, premature restart, and later-read cancellation after a complete ACK.
  Revised f72ee2f requests server closure after ACK and waits for ordered DONE
  before authorizing native writes; pending cancellation is processed first.
  Restart also waits for closure, so an old Cancel cannot target the next job.
  Recovery/Fcitx/UI pass in 36.52 s; follow-up review reports no remaining
  Important or Critical findings. Final full build succeeds and all 12 CTest
  targets pass in 88.47 s at the same production source. Windows CI remains
  pending. See the operation UI plan for exact regression evidence.
- **PR26, operation deadline:** storage-access now holds
  codex/fcitx-operation-deadline based on verified PR25 checkpoint 55aed4f.
  Test-only c4cc5cb accepts the real shortcut/field/operation lock, then delays
  controlled worker-result delivery. An immediate result must commit once; a
  result delivered after 10.1 seconds must leave the original intact. Hosted
  RED 39542141953 fails only the late-commit assertion. Fix 635932e carries
  one precise monotonic deadline from acceptance through setup, worker and
  guarded native commit. Full push/PR runs 39542671464/39542677136 pass Linux
  and Windows; downloaded Fcitx JUnit has 13 cases and no failures/errors/skips.
  Independent static review found no important issue. Scoped deadline work is
  complete; UI Cancel and ordinary Wayland acceptance remain separate.
- **Current work mode:** the user explicitly resumed full implementation and
  requested local tests where practical to reduce downloads. One-job bounded
  local builds are being reintroduced using existing dependencies (1 GiB per
  process, CPU/time limits, nice 19); no parallel build or desktop workload.
  The complete source build and all 12 registered CTest targets passed locally
  in a 1200 MiB aggregate cgroup with no permitted swap, CPUQuota 80%, software
  rendering and serial execution. CTest took 178.04 seconds. Prior host freezes
  remain unexplained; this is not general host-stability acceptance.
- **Fcitx recovery work:** storage-access now holds codex/fcitx-original-recovery
  based on ae6ecd7. Test-only real-acceptance regressions require original
  delivery before native commit, no write after disconnect/revocation/selection
  change/expiry, and history only after confirmation. Local RED 9c05958 fails
  all six new cases (2.901 s). Implementation now waits for authenticated local
  delivery ACK while retaining the operation lock/deadline, rejects older ACK
  overlap, and rechecks selection/policy before write. Local core/recovery/Fcitx
  suites pass in 22.69 s; Fcitx JUnit has 22 cases and zero failures/errors/skips,
  including added Cancel/password/older-ACK controls. The complete serial build
  and all 12 CTest targets pass at 611be89; the full test run took 178.04 s.
  Independent static review found no important finding. Draft PR28 is stacked
  on PR27. Push/PR run 39575913891 at 6b76ae4 passes Linux and Windows in
  3m20s: Windows build/tests, dependency metadata, installer build,
  install/launch/uninstall and native field/shortcut probe all pass; Linux
  build/CTest, UI, foreign-server rejection and controlled GTK probes pass.
  Ordinary Wayland acceptance and GUI IPC Cancel remain pending and separate.
- **PR27, native operation UI:** storage-access holds codex/native-operation-ui.
  Tested source a6c2471 passes full Linux/Windows push/PR runs
  39544490522/39544493641. Downloaded UI JUnit on each OS: 26 cases, zero
  failures/errors/skips. Real apply/Cancel/unchanged paths pass, including
  visible workspace Cancel, no write/history after cancellation, and terminal
  busy cleanup. Independent reviews found no important issues. Initial e905973
  RED reproduced missing busy on both OS. The first fix then exposed an
  ambiguous test lookup of dialog Cancel buttons; unique workspace selection
  resolved it. No local C++/Qt workload ran.
- **Operation UI acceptance:** PR27 closes the X11/Windows Service path.
  Fcitx lifecycle/Cancel implementation is now under final verification above;
  ordinary Wayland desktop acceptance remains separate. PR28 addresses original
  delivery before native writes while preserving confirmed-write-only history.
- **PR25, Fcitx:** real six-case RED at d1e70c7/run 39538586697 confirms retained
  owner job text on unsafe observation. Scoped reset 7ad9a99 passes the complete
  Linux/Windows run 39539160624 and independent static review. Worker/future and
  operation-lock lifetime are preserved; cancellation remains cooperative.
  Timer-only policy fix and corrected fixture at cc13055 pass full Linux/Windows
  push/PR runs 39540890991/39540896152. Actual Fcitx JUnit: 11/11 passing cases.
  Negative proof 93326bf/run 39541257155 removes only the timer guard and fails
  exactly Pause/denial/lock cancellation with the same valid loop controls.
  Do not merge codex/fcitx-timer-red-proof. The periodic witness proves timer
  policy behavior, not a production 160 ms bound; native accuracy stays 250 ms.
  Reviews found no important issue. Deadline/UI-cancel coordination and ordinary
  Wayland acceptance remain open. See the Fcitx plan for exact evidence/limits.
  Branch codex/fcitx-transient-guards is pushed and stacked on PR23; its managed
  worktree has been reused for PR26.
- **PR24, workload diagnostics:** 2aa59d3 passes full push/PR Linux/Windows runs
  39538716534/39538809182 and independent static review. Two small diagnostic
  tests and 18 existing active-stability tests pass locally under 256 MiB/10 CPU
  seconds; no local C++/Qt workload ran. Manual run 39539196812 at that source
  started a same-AppDir preflight before the instrumented two-hour workload.
  Inspect its existing handle rather than relaunching on an observation timeout.
  Keyboard-review holds codex/active-workload-diagnostics; it is stacked on PR20.
  Final run 39539196812 FAILED after 4,206.262 seconds (about 70 minutes),
  stage selected-history, failure test-infrastructure; 1,471 full cycles.
  The last live first/second/third/Undo counters are all 1,473 and the last
  third-attempt state is corrected. This does not reproduce the prior second
  retry mismatch, nor establish its root cause or two-hour acceptance.
  Helper cleanup required escalation; cleanupFailed is false. App CPU 4.26%,
  helper 1.45%, zero swap, p95 upper bound 22.6 ms across 2,946 samples.
  Binary SHA256 b7683b398b33ef6e26ff03be93679be907ce4293357d8683b6d64eedafe10e70.
  Actual report: .deps/hosted-active-2aa59d3/active-workload/report.json.
  The initial detailed log fetch timed out at TLS. A later bounded direct job-log
  fetch succeeded: the workload prints no exception subtype/traceback, only
  exit code 1. Therefore the infrastructure cause remains undetermined.
  Artifact 12320759229, archive SHA256
  8bc06d40d2bfa490446401af979ea7d9408d76f5e687695891764833053b7d2f.
  Local ignored log: .deps/hosted-active-2aa59d3/linux-job.log. No blind rerun
  or guessed production fix; investigate after the reduced-work mode.
  Short source audit: active_stability.py catches WorkloadFailure separately
  from generic Exception. selected-history waits through HistoryCheck, whose
  read-only SQLite connection uses a 0.5-second timeout. A normal wait expiry
  is classified operation-timeout by stability_evidence.wait_until, so the
  generic report does not establish a selection wait timeout. SQLite/read,
  sampling or report-writing exceptions remain possible; none is confirmed.
  Next diagnostic should record only a fixed exception category, without
  exception messages, commands, paths or passage text. No runtime code changed.
- The earlier long-run mismatch is still unexplained. No production correction
  logic, typing delays, resource budgets or pass assertions changed in PR24.
  Prior 632-cycle partial evidence is not two-hour acceptance. PR20's GitHub
  description now records the failure. No release, installation or shutdown.
- **PR23:** matching Mesa and D3DCompiler SDK archives are verified against
  official Qt SHA256 metadata. Each sole DLL matches the actual staged bytes
  from run 39399260940. Receipts through 1c423bf contain no host paths; neither
  DLL was loaded/executed. The archives contain no notice text. Source/notice
  closure and Microsoft signature/redistribution material are still open.
  Documentation-only parent receipts are now merged into this guard branch;
  application/test source remains identical to tested cc13055.

The pause record and older checkpoint below are historical and superseded by
this resumed status. Main develop remains 15e8ad5; PR20–27 remain unmerged.

## Pause checkpoint — 2026-10-20

- Main develop is unchanged at 15e8ad5. PRs #20–#23 remain unmerged drafts.
- PR22 code cba5167 passed full Linux/Windows run 39400869152. Its actual
  long-data captures were inspected; documentation fb1020d is pushed.
- PR23 metadata code db871eb passed full runs 39399260940/39399290932;
  documentation/source-correspondence receipts through 1605c0c are pushed.
- The two-hour run 39394646024 at 0fd1f2e FAILED after 1,819.969 seconds,
  during the live retry sequence: native-text-mismatch, 632 complete cycles.
  Its first/second/third counters are 634/633/633. No duration-complete event.
  Helper cleanup required escalation; app CPU 5.86%, helper 2.11%, no swap.
  Word p95 upper bound 23.6 ms is partial-run evidence only. Binary SHA256
  037a68b4e5f9173f39325d1d7ff1002d99b78f0b208215e765d52ff92621c131.
  Actual reports are ignored under .deps/hosted-active-2h-0fd1f2e. Do not claim
  two-hour acceptance or blindly rerun: first add content-free attempt/timing
  diagnostics and determine why the expected second attempt did not match.
- Storage-access holds codex/fcitx-transient-guards at f478026 before this
  checkpoint. It is pushed, with a plan and a real Fcitx/Store/recovery fixture.
  Production behavior is unchanged; only friend access was added for inspection.
  First CI 39482104028 failed to compile the test's capability-flag expression;
  f478026 fixes that fixture expression. No behavioral RED or fix is accepted.
  Inspect its latest hosted outcome before editing the addon after resume.
- Keyboard-review holds clean local codex/active-workload-diagnostics at ef5db74,
  with no diagnostic implementation yet. PR22 remains on its pushed branch.
- Local C++/Qt/heavy workloads remain suspended after the two hard freezes.
  No installation, release, PR merge or shutdown was performed at this checkpoint.

The next checkpoint section predates this pause; its pending two-hour statement
is superseded by the failed result above.

## Read this checkpoint first

- **Host safety:** two hard freezes required hardware resets. Cause remains
  unknown. Local C++ builds, GUI workloads and heavy tests stay suspended; use
  hosted CI. Do not infer host safety from a passing cloud run.
- **PR20 — workload and CPU:** branch `codex/hosted-active-workload` at ef5db74
  (documentation after code candidate 0fd1f2e). Full hosted run 39393887050 passes
  Linux/Windows and the packaged 120-second workload. App CPU 3.44% of one core,
  main thread 1.86%, helper 1.22%; word p95 upper bound 22.4 ms. Two-hour run
  39394646024 at 0fd1f2e passed its same-package preflight and started its long
  workload at 08:42:48 UTC. Its final outcome is pending. Keep earlier failed
  CPU/cleanup evidence. No budgets were relaxed.
- **PR21 — guarded native state:** full run 39396472858 passes Linux/Windows at
  4cc4090: reset/Undo boundaries, post-write authorization and recovery,
  expected-target native reads, unsafe native-job cancellation and queue-inclusive
  ten-second deadlines. Each defect had an observed hosted RED regression.
  Independent static reviews found no important remaining introduced issues.
  Combined-source push run 39397165863 and PR run 39397184524 both pass
  at merge 41939dc. Documentation-only updates follow that tested code.
  Final package/desktop acceptance remains separate.
- **PR22 — populated UI:** branch `codex/populated-list-review`, code cba5167,
  reuses the keyboard-review worktree. Full Linux/Windows run 39400869152 passes
  after observed overflow/contrast failures. Actual regenerated captures confirm
  the sampled long list values and Practice question fit; see its UI_REVIEW.md.
- **Build metadata:** branch `codex/windows-build-metadata` follows PR21. Four
  observed Python RED cases now pass with eight existing controls. Exact vendor
  metadata retention is limited to 1 MiB/file and 8 MiB total. Full hosted
  runs 39399260940/39399290932 pass at db871eb; documentation revision 4e73a33
  also passes 39400501572/39400507867. Actual retained metadata: 158 files,
  186,979 bytes, all matching reported sizes/hashes. Hunspell and libiconv
  source/recipe/ABI receipts are under docs/evidence. These match cache records;
  no independent reproducible build or complete source/notice closure is claimed.
- **Worktrees:** storage-access now holds the build-metadata branch based on PR21;
  keyboard-review holds PR22. The main
  checkout remains `develop` at 15e8ad5. Check status before changing branches.
  Keep ignored `.deps` evidence and dependencies out of commits/uploads.
- **Next:** audit the separate Fcitx cancellation/buffer path, finish the pending
  two-hour report, and maintain the source/run/hash
  distinctions. The workload will need repeating on the final integrated package.

## Remaining product acceptance

Ordinary Windows 10/11 desktop/tray/scaling and broad GNOME Wayland/Fcitx use;
populated UI and remaining keyboard/screen-reader checks; final current-host
latency and packaged active stability; exact final runtime/source/notices binding;
clean reproducible AppImage/.deb/.exe installation and ordinary-user documentation.
Release and local installation happen only after these gates pass. The user's
shutdown instruction is conditional on finishing the work; that condition has
not been met. Do not shut down merely because CI is green.

Cancellation is cooperative. No secure QString erasure or preemptive interruption
of a native provider is claimed. Expired results are refused before application;
a blocked worker/provider can delay the refusal. User history/session recovery
remain explicit, bounded and separate from transient live observation. The Service
regressions do not establish universal password or application support.

The sections below are an append-only evidence trail; their older pending/paused
statements describe those revisions, not the current checkpoint above.

## Historical handoff — 2026-10-18

## UI branch checkpoint — 2026-10-20

PR #22, codex/populated-list-review, is stacked on PR #20. At cba5167 the
complete Linux/Windows run 39400869152 passes, including 29 Linux UI cases,
installer and controlled native checks. Actual downloaded captures confirm the
sampled long list values and Practice question wrap without covering actions;
dark ComboBox indicator contrast also passes. See docs/UI_REVIEW.md and the
2026-10-20 populated-list plan for observed RED cases and final inspection.
Physical desktop/scaling/tray and full product acceptance remain open. Local
compilation and Qt workloads remain suspended after the two host freezes.
The older narrative below is historical evidence, not current release approval.

## Workload investigation checkpoint — 2026-10-21

The user resumed work. PR20's two-hour run 39394646024 FAILED after 1,819.969
seconds at the second live retry attempt (634/633/633 first/second/third counts),
after 632 sustained cycles. Helper shutdown needed escalation. The preflight
passed; long duration acceptance remains open. Reports are preserved in ignored
.deps/hosted-active-2h-0fd1f2e. Do not transfer partial latency/resource results
into a two-hour success claim.

Keyboard-review now holds codex/active-workload-diagnostics based on ef5db74,
with bounded content-free attempt diagnostics and two tiny tests. All 18 existing
active-stability fixtures and both new cases pass under resource limits. Hosted
verification and an instrumented rerun are pending. Production correction logic,
delays, assertions and budgets are unchanged. Main develop remains 15e8ad5;
PR20–23 remain drafts. Local C++/Qt/heavy workloads remain suspended.

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

## Keyboard/dialog UI and latest delivery instruction

PR #13 merged as 6735cd2 after final-head Linux/Windows runs 39135443045 and
39135448262 passed. Its Windows material receipt is explicit about unresolved
runtime license/source binding; no product release has been accepted.

The managed keyboard-review worktree uses codex/dialog-keyboard-focus. It adds
keyboard modal/sidebar/No acceptance checks and a one-line wrap fix for long
executable names in the permission heading. The long-title geometry regression
was RED; focus/navigation already worked. CI now collects synthetic Linux/Windows
page/dialog captures. All seven local CTest suites pass in 38.63s. See UI_REVIEW.md
for the exact visual evidence and fixture/evidence limits.

The user now explicitly requires ordinary-user GitHub installation/use docs,
a real release only after quality/stability is established, and installation of
that accepted build on this current machine. These are recorded in REQUIREMENTS.
Do not install a development build or publish a product release merely because CI
is green. Repository visibility remains intentionally PUBLIC.

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

## Current UI repair checkpoint

PR #15 source delivery docs merged as c4e7fb3 after final Linux/Windows runs
39154247772/39154250601 passed. UI work has incorporated that documentation.
At 63e1fc2 Windows tests pass but its offscreen screenshots contain missing glyphs;
Linux Qt 6.4 still fails four popup header contrasts. Explicit heading/body theme
bindings and a shaped-glyph regression are now added. Local build and all seven
CTest suites pass in 66.17s; fresh CI and rendered review remain required. Three
isolated native X11 cycles pass. See UI_REVIEW.md for the actual failures and
limits; no final UI/platform acceptance or product release is claimed.

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

## GTK fixture timing/watchdog — 2026-10-19

The storage-access worktree now uses codex/fixture-watchdog-timing. The GTK test
fixture validates a 1–18000-second lifetime (default 120) before GTK initialization
and optionally emits CHANGE sequence/monotonic-microsecond records without text.
The owned desktop runner verifies expiry, two synthetic changes and quiet mode;
--fixture-timing-only runs this specific check without general command execution.

Review required timestamps from actual native edits, beyond programmatic GTK
changes. The Service probe now enables and validates records for first/third
actual corrections and verifies no protected operation change. Missing native
records were RED before enabling instrumentation; the complete resumed run passes
all three native/service cycles. Final independent review has no blocking issue.
Full build and all nine CTest suites pass in 35.77s. See the active-stability plan
for exact scope: packaged GUI workload, latency baseline and multi-hour active
acceptance remain open. No product release or local install was performed.

## 2026-10-19 resumed verification

PR #16 merged as d352e45 after final Linux/Windows CI 39155478705 and
39155551787 passed. UI source 83ca324 also passes both OS runs 39155786400 and
39155790704. Downloaded Windows Overview/History detail and both ten-dialog
contact sheets show correct English/Persian glyphs and readable text. A final
common footer surface adjustment has passed local modal/No checks and review;
final integrated eight-suite build/CI is next. No ordinary desktop acceptance,
product release or installation is implied. See UI_REVIEW.md for remaining scope.

PR #14 is now integrated as 48e3d7f after final-source d4a1e37 passes Linux/Windows
CI 39239381403 and39239387818. Final Linux light History/Windows dark Pause
captures were downloaded and inspected; the footer repair is confirmed. Local
integrated eight-suite verification passes in46.32s. The fixture/timing branch
is incorporating that UI source; its final integrated checks are next. Windows
normal desktop and the other product release gates remain open.
## Paused checkpoint — 2026-10-19

The user requested stopping as soon as the current tests or a reasonable stage
finished. Work is paused; do not resume development, release or installation
until explicitly requested. Uncommitted Task 3 work is preserved on
`codex/active-gui-workload` in the managed keyboard-review worktree.

The first short packaged-GUI workload failed (`.deps/active-baseline-01/report.json`):
native-text-mismatch, forced shutdown, four word-boundary latency samples with
p95 upper bound 56.7 ms. It is not a passing stability result. The app averaged
105.18% of one CPU over the reported 19.13-second resource window; investigate
that observation before any sustained-run acceptance claim.

The cleanup review finding is addressed in the working tree: owned fixture/app
shutdown now escalates TERM to KILL with bounded waits, helper cleanup uses its
pinned pidfd with bounded exit observation, and individual cleanup failures do
not skip the remaining resources. Signal-handler restoration is protected even
if report publication fails. Cancellation during cleanup invalidates success.
New tests use real owned subprocesses ignoring SIGTERM. The missing-cleanup
tests were observed RED (`.deps/active-cleanup-red.log`); all 15 active-stability
unit tests now pass (0.389 s). This is targeted evidence only: no full CTest,
fresh desktop rerun or independent follow-up review has been performed after
this cleanup change. No commit, push, release or installation was made here.

Task 3 remains incomplete. Before acceptance: strengthen oversized-selection
evidence with a correctable typo and below-limit counterpart; confirm each
history insertion by advancing ID and exact original/result pair; count actual
successful role samples; force final process/cancellation checks; diagnose the
native-text mismatch, CPU observation, latency and forced app shutdown; rerun
the owned packaged workload and full suite, then obtain follow-up review.
The final multi-hour workload and all remaining release gates remain open.

## Resumed active-workload verification — 2026-10-19

The user explicitly resumed. The paused checkpoint above is historical.
All ten CTest suites pass under system Python 3.13 (65.52 s); the cached local
Python 3.11 build lacked pidfd APIs and initially failed two cases. Acceptance
runs require these APIs. All 17 dedicated workload tests pass, independently
repeated by review. The harness review found no new blocking defect; verifying
the running GUI executable after launcher exec was suggested as extra hardening.
The inspected generated AppRun currently execs the exact hashed packaged binary.

Baseline 07 completes every functional scenario, including the new correctable
49,998-character control, rejection of 50,001 characters, password and Pause
exclusion, 113 exact verified history inserts with pruning to 100, live retry
and Undo. App/helper shutdown is normal, with no cleanup failure. Its six actual
word-boundary samples have p50 upper bound 46.0 ms and p95 89.2 ms; the report is
FAILED for latency, not accepted as a stable release. Dense history-burst CPU
is retained separately; see the active-stability plan for phase budgets/rulings.

The intermittent initial live timeout was addressed by waiting for the GUI's
normal window to be mapped before starting the fixture. GTK clear commands now
have an acknowledgment before Pause typing. Baseline 06 was a runner syntax
error, fixed before 07; it has no acceptance value. The Service's 80 ms poll is
now changed to 20 ms in the working tree, and a freshly staged development
package is being measured. The final full suite, native safety rerun, CI, Git
integration, sustained CPU/latency result and multi-hour final-package run remain
pending. No product release or installation has occurred.

## System restart and shortcut root cause — 2026-10-19

The user reported a system crash and requested continuation. After restart,
all changes are present; no Reword/test/build process remains. The accessible
journal cannot establish the crash cause, so do not attribute it to this project
or claim the system crash fixed. Use serial, bounded verification; no multi-hour
run is currently active. The 18-test diagnostic-era unit run passed after restart
(0.386 s); the temporary notice observer/classifier is now removed, leaving the
17 workload tests plus the new numeric last-snapshot assertion.

The 20 ms candidate reduced early word samples, but selected actions timed out
in the dense history workload. A temporary read-only GUI diagnostic on the
owned desktop returned only the fixed `unsupported-selection` notice code in
`.deps/active-poll20-04/report.json`. It collected no text into the report.
The synthetic setup delay did not resolve this issue and is not a proven fix.

A deterministic native regression holds Ctrl+Alt+R for 200 ms, then releases it.
The baseline fails: the shortcut is acted on during the X11 passive grab, and
the selection is rejected as unsupported even though focus returns afterward.
The source fix dispatches after key release and a 20 ms accessibility interval,
coalesces held-key repeats, bounds pending actions to eight and event processing
to 64 per poll, and clears pending actions when shortcuts are reconfigured.
It requires detectable XKB auto-repeat; unsupported registration fails closed.
The new native regression is GREEN for one complete owned probe cycle; see
`.deps/shortcut-release-red.log` and `.deps/shortcut-release-green.log`.

Fresh runtime review, complete CTest/native rerun, a newly staged package and
active latency/resource verification are still required before claiming this
change accepted. Existing packaged poll20 binaries predate the release fix.

## Second hard freeze: remote verification — 2026-10-19

The user confirmed full-system hangs requiring the hardware restart button,
not just application crashes. The second interrupted build stopped during UI
test compilation, without a compiler error or completed test result. Its process
handle is missing and process inspection confirms nothing remains alive.
Do not count this interrupted execution as GREEN or restart it automatically.
Accessible logs contain no crash-cause evidence; current-boot memory counters
cannot establish or exclude prior-boot OOM, GPU or kernel failures.

All local builds, GUI workloads and heavy tests are suspended. Continue code
review, Git checkpoints and hosted CI without local heavy verification. The
harness checkpoint `67ef91b` is pushed to the public feature branch; no product
release or installation has occurred. The earlier post-first-restart ten-suite
run passed in 45.16 s, before the latest pending-shortcut follow-up changes.

Runtime review found held Undo losing its buffer and an older queued action
dispatching while a later chord was still held. Both were reproduced RED in
owned native tests (`held-undo-red.log`, `queued-shortcut-red.log`). Five extra
metadata-only Service boundary cases also failed before the fix (password,
locked, unknown, denied, paused). New code waits for all pending releases and
their grace intervals and suppresses text observation while a shortcut is
pending. It still checks metadata/policy and erases Undo at unsafe boundaries.
Compilation of the helper/application completed before the second freeze, but
the full build and post-fix tests did not. Final verification is moving to CI;
the follow-up remains unaccepted pending those results and review.

Runtime source bd3d25d passes Linux and Windows in hosted push run 39309952986,
including the new native regressions and metadata security cases. The separate
PR run 39309999956 passes Windows but fails Linux before native tests begin:
Openbox does not publish its readiness property within the runner's five-second
startup budget. This is retained as a failed run, not overwritten by the green
push. The runner now allows 15 seconds for window-manager initialization, bounds
each readiness command to one second and emits only bounded Xvfb/Openbox startup
logs if it still times out. No app/fixture has started at that point. The reason
for the original slow/missing readiness is unproven; fresh CI must verify this
infrastructure adjustment. Word-latency limits are unchanged.

The user authorized continued unattended work and shutdown once work is finished.
Preserve the local-heavy-work suspension after the hard freezes. Do not shut down
while owned work is live or treat incomplete release/installation gates as done.

## Hosted native acceptance and Windows dependency evidence — 2026-10-19

PR18 merged as ffd6143857693d3f718c33fc0e9e7097c0dc0f3b. Final source
a29db456c7e08064048eee176d0170f86f73c6eb passed Linux and Windows in hosted
runs 39311726138 and 39311733695. The latter log confirms three complete owned
native shortcut cycles, including held Undo and overlapping shortcut releases.
This resolves the pending CI gate above, not the packaged active soak/current
host latency, ordinary Windows desktop, release or installation gates.

The Windows PE dependency branch's first real run 39314256438 passed the
application tests but failed bundling on an imported name rejected by the reader.
No installer was produced. The reader originally accepted only .dll; Windows'
documented print spooler is WINSPOOL.DRV. Two new synthetic regressions establish
safe .drv parsing and explicit-system byte matching; actual rerun must determine
whether this resolves the real input failure. No dependency check is bypassed.

Independent review also found that PE collection errors prevented retaining the
otherwise trusted materials inventory. A CLI regression reproduced the missing
report; collection errors now produce an explicit incomplete failure record and
still stop packaging. All 25 PE fixture/CLI tests pass in 0.123 seconds under the
256 MiB/10 CPU-second limit. Local builds/GUI remain suspended; fresh hosted CI
and review are required for these follow-ups.

Run 39315174646 at 0561148: Linux passes; Windows tests pass but packaging
correctly fails the new dependency gate. Downloaded its retained audit to
.deps/windows-pe-ci-0561148. Collection completed for96 staged PE files, with
no ambiguous imports; missing libpq.dll and mimapi64.dll come solely from unused
PostgreSQL/Mimer Qt SQL plugins. Store explicitly opens QSQLITE. The follow-up
retains qsqlite.dll and removes other SQL driver DLLs, leaving graphics intact.
Installed smoke acceptance now requires the synthetic SQLite database/header
after native clean-PATH launch. This follow-up needs fresh Windows CI.

PR #19 merged as 15e8ad5c3ebbaa5348bde098d58363e3b23da7ab after both hosted
runs 39315937141 and 39315944368 passed at 7284138. Linux: 10 CTest suites plus
three native cycles. Windows: 6 CTest suites, 25 PE tests, installer/native clean
PATH launch, SQLite header, uninstall and native field/shortcut tests pass.
The downloaded audit has 93 PE files, 89 candidate names, 16 separate API-set
contracts, no missing/ambiguous ordinary imports and only qsqlite.dll as a SQL
driver. Its SHA256 is 5cc59ae4ed660092f34f082290a0a226afe2ec29beac56bb95ac30061f612d49;
artifact 12218965126. Product release readiness remains false. No local install.

## Hosted packaged workload — 2026-10-19

PR #20 adds an explicitly selected manual workload duration to the existing CI;
normal push/PR runs do not start it. Runs39315576445 and 39316211634 refused
staging before the active workload. The retained scanner JSON identifies a
QtQml directory without module metadata, while CI installed WorkerScript but
not QtQml/Models. The official Ubuntu module archive owns the missing metadata
and plugin. Added QtQml, Models and Shapes packages; run 39316879658 at 56138e6
is pending. Keep the path/formal-module validation intact. Do not count failed
staging as an active soak. Ubuntu CI evidence cannot establish current-host
Debian latency or final-package acceptance.

Source audit follow-up before release: Service clears RetryCycle on unsafe
boundaries but leaves retryPrefix_/retryWord_ QString references, and exception
paths do not uniformly clear Undo/retry payloads. Review temporary-buffer reset
ownership and add meaningful regression coverage; this is an identified open
privacy requirement, not a claim that password text was observed or persisted.
All local builds/GUI/heavy tests remain suspended after the hard freezes.

The actual packaged run 39316879658 at 56138e6 fails the resource budget after
65.418 s, not the requested 120 s: sustained app CPU 15.90% of one core after
30.13 s, helper 2.51%, zero swap, app max RSS 116424 KiB and private growth 176 KiB.
Normal cleanup completed without forced shutdown. Word 23 samples: p50 upper
bound 16.1 ms, p95 upper bound 22.3 ms, max 25.261 ms. It completed 110 initial
history insertions and 10 full cycles, but never reached final selection-limit
checks. This is failed evidence, not an accepted short run or soak.
Binary SHA256 0e24b92b94ab149355d256dcae7a88d9c972a33739ae55cb7b0a714be6604206;
helper 0fac4152b1dae0ab03a1bab1e96a38ce035b094fceb1f090afb6877362ed58bd.
Report: .deps/hosted-active-56138e6/active-workload/report.json.

Service constructs and destroys a Hunspell-backed Engine on every selected-text
job. Before changing production code, explicit performance-probe mode compares
ten fresh-engine passages with ten passages on one engine, collecting CPU/wall
totals and exact synthetic correctness. Hosted run 39318112831 at 019591a is
pending. Treat initialization cost as a hypothesis until its measurements arrive;
keep the CPU and latency budgets unchanged. No long soak started.

Run 39319638562 at 31ab2cd passes Linux and Windows plus the full 120-second
staged workload. App SHA256
0c3aaade2fca52d92712efee130a1064e6002630bb6f8e9e2a8a7e810dcaf22c;
helper unchanged 0fac4152b1dae0ab03a1bab1e96a38ce035b094fceb1f090afb6877362ed58bd.
44 full cycles, 155 selected fixes/history inserts, both 50k control/refusal
checks, all Pause/password/Undo checks and normal cleanup pass. Word 90 samples:
p95 bound 21.8 ms/max 22.505 ms; sustained CPU app 6.64%/helper 1.06%, swap zero,
private growth 1316/1224 KiB. Report:
.deps/hosted-active-31ab2cd/active-workload/report.json.
Independent review found no important issues in isolated passage-engine reuse
and deterministic harness cleanup; 18 tiny harness tests pass. Full hosted runs
39320326333/39320337032 at afa301b also pass the added C++ cancellation recovery
regression. The manual 7200-second mode now first runs a 120-second preflight on
the same staged AppDir and advances only on success. Both reports are retained.

Do not claim the earlier shutdown-forced baseline's root cause fixed from one
normal exit. Keep product release, source/notices, privacy-buffer audit,
current-host latency/final-binary soak and ordinary Windows/Wayland gates open.


## Transient privacy reset — 2026-10-20

Stacked branch codex/transient-observation-reset in the storage-access worktree:
99f91d3/run39321745068 reproduced seven retained-text boundaries and stale Undo
after inspection failure. d27d3f8 centralizes retry/observation cleanup and passes
Linux. Windows' failure was silent until Service JUnit output was added in
633e9ed. Diagnostic run39391142137 proves its only failure is the exception
retention row: safe polling can legitimately refill previous_ after the reset.
The test now quiesces its synthetic adapter in the throwing callback. Fresh
Linux/Windows verification remains pending. Static independent review found no
important production reset regression; post-write read identity/authorization
and native-job cancellation audits are still separate open privacy gates.
No local build, GUI workload, product release or installation occurred.

## Privacy checkpoint — 2026-10-20

Task1 reset/exception regression passes full Linux/Windows run39391695577 at
2d88084 after quiescing the exception fixture before safe polling resumes.
Partial Task2 post-write authorization/recovery passes full run39393054682 at
58959a8 (13 new regressions). Independent review found an introduced recovery
signal cancellation gap: five hosted RED cases at4853ba6/run39393873509 each
write after policy revocation. Recheck immediately after recovery signal and
before the write; fresh hosted GREEN pending. Session recovery is explicit,
bounded and independent of persisted history; availability does not assert a
confirmed edit. Adapter metadata-to-read identity enforcement and cancellation
of stale native worker captures remain open in the transient-observation plan.
No local C++ build, GUI workload, release or installation was performed.

HostedRED39394490601 atc326c84 reproduces four metadata-to-read changes:
application,field,password and unknown each request text once when zero reads
are required (67 other Service cases pass). Linux log retained as
.deps/expected-read-red-c326c84.log. The new readExpected default refuses;
owned native probes require both a valid guarded read and rejection of wrong
app/field identity. Native probe GREEN remains pending.

Service now passes the authorized metadata to readExpected. Linux sends only
app/ID over its private helper pipe; inspect requests are metadata-only and the
separate read operation requires identity. Linux snapshots pin one referenced
AT-SPI object through metadata/text, compare app/ID before text interfaces,
and retain that same validated object through the write. Windows compares
expected identity on its pinned UI Automation element before any text read.
Native pre/post-write reads also use expected identity. All existing native
password/unknown/lock/plain/editable/size checks remain. Hosted full tests and
independent review are required; queued native worker cancellation stays open.

Cancellation-gap fix a2f9ba2 passes full Linux/Windows run39394352890.
Independent follow-up review confirms its P2 finding resolved; static review
is distinct from native desktop acceptance.

## Native job cancellation checkpoint

Full Linux/Windows run 39395053416 passes at f495be3, including guarded native
reads and controlled platform probes. Independent static review found no
important introduced issue in the expected-target implementation.

Hosted RED run 39395433922 at b8a5171 reproduces all eight stale native-job
cases: no cancellation is requested after unsafe metadata, denied permission,
Pause, changed identity or a metadata exception. The workspace control passes
and 72 other Service cases pass. Saved log:
.deps/native-job-red-b8a5171.log.

Track only the active native job's app/field identifiers. While it is pending,
metadata must still match that target before reading text; unsafe transitions
and integration exceptions request cooperative cancellation. Pending shortcut
focus grace remains allowed only for the same safe target. Workspace jobs do
not inherit this external-focus cancellation. Clear job metadata at completion,
stop and failed startup. Full hosted GREEN and review remain pending.

Cancellation does not promise secure memory zeroization or instant removal of
QString copies held by an already running worker. Worker queue time and the
operation deadline still require a separate regression audit.

## Queue-inclusive operation deadline

Hosted run 39395969290 at f1984bf reproduces the queue deadline defect: after
10.1 seconds waiting for a worker, the operation still succeeds. All 80 other
Service cases pass on Linux, including native cancellation and its workspace
control; Windows Service also fails. Retained failure log:
.deps/queue-deadline-red-f1984bf.log.

Use one monotonic QDeadlineTimer starting when an operation is accepted. Check
expiry before worker initialization, pass only its remaining budget to Engine,
and refuse an expired result on completion or immediately before a native write.
A cancelled generation is also rejected before initializing the passage engine,
even if a later prepare() clears the shared cancellation boolean. No partial or
late correction may be delivered. Full hosted verification remains pending.

The ten-second budget bounds acceptance of a result; an unresponsive provider
or a saturated external worker pool can still delay delivery of the refusal.
Cancellation remains cooperative and no secure memory-erasure claim is made.

## Resume checkpoint — 2026-10-20

The user explicitly resumed. Local builds/GUI/heavy tests remain suspended.
Manual run 39321497233 at 8570675 failed its short preflight on app CPU 14.11%
after 30.13 sustained seconds, with word p95 bound 24.8 ms, zero swap and normal
cleanup. Its two-hour step did not run. Retain this failed evidence beside the
passing 31ab2cd baseline; engine reuse alone does not close the CPU gate.
Report: .deps/hosted-active-8570675/active-preflight/report.json.

Main-thread CPU diagnostics now collect only numeric counters from the pinned
owned processes, separately from aggregate app/helper CPU. The same post-burst
baseline applies and both burst counters are retained. Five tiny diagnostic
tests pass in 0.004 s and 18 harness tests in 0.383 s under 256 MiB/10 CPU seconds.
No production optimization or acceptance limit was changed. Hosted attribution
and the full suite remain pending.

The stacked privacy branch codex/transient-observation-reset lives in the
storage-access worktree. Hosted RED 39321745068 at 99f91d3 reproduced seven
retained-text boundaries plus stale Undo after an inspection error. Reset fix
d27d3f8 passes Linux; Windows Service failed without Qt stdout. Commit 633e9ed
adds Service JUnit artifacts to identify the actual failure before another fix.
Independent static review found no important production reset regression, but
identified a test timing race in the exception row (safe polling can repopulate
observation). Whether this caused the Windows failure awaits its XML. Post-write
authorization/read identity and native-job cancellation audits remain open.

## Shared-settings workload checkpoint — 2026-10-20

Hosted39391593030 at7df1da1 attributes10.65% main-thread CPU out of13.50%
aggregate app CPU. A real QML regression at859b133/run39392713956 reproduces
37 full settings reads per notification; each read issues roughly25 SQLite
queries. Main.qml now takes one reactive settings snapshot and passes it to
all20 SettingRows. The test follows the Switch's direct parent relationship
instead of inspecting nested style internals; unchanged <=2-read assertion
and false/true language reactivity both pass on Linux and Windows.

Full manual run39393887050 at0fd1f2e passes both platforms and the complete
120-second packaged workload. Report .deps/hosted-active-0fd1f2e/active-workload/report.json:
149.133s including setup/cleanup,43 full cycles,154 selected fixes/history
insertions,history pruning,50k control/refusal,Pause/password/Undo and normal
cleanup (forcedProcesses=[]). Word88 samples p95 conservative upper bound
22.4ms,max25.286ms. Sustained appCPU3.439%,mainthread1.863%,helper1.223%;
zero swap,private growth3040/1308KiB. App SHA256
bd86aee2d0a1267c089096ccb94ded0599cd680833f79a18a173b17228822d0d;
helper0fac4152b1dae0ab03a1bab1e96a38ce035b094fceb1f090afb6877362ed58bd.
Independent review of the snapshot and numeric sampler found no important
issues. No budgets were relaxed. Two-hour run39394646024 was dispatched at
the unchanged0fd1f2e revision, with its own120-second same-AppDir preflight.
Its outcome is pending. This is hosted Ubuntu evidence; final merged-source,
current-host,ordinary Windows/Wayland,distribution and release gates remain.

## Combined Service and workload verification

Merge 41939dc preserves both parent evidence trails and combines PR20's
single-snapshot UI/metrics with PR21's native guards and deadline. Full Linux
and Windows push run 39397165863 and PR run 39397184524 pass, including controlled
native probes and installer smoke checks. Independent reviews of each behavioral
change found no important outstanding issue. These are hosted development checks,
not ordinary desktop or final-package soak acceptance.

## Windows metadata retention checkpoint

The existing audit retained notice contents but only hashes of Qt/vcpkg build
metadata. Four observed RED fixtures preceded bounded exact retention under
build-metadata. All 12 targeted Python fixtures pass in 0.186 seconds under the
local 256 MiB/10 CPU-second limit. No local C++/GUI work ran. Source/ABI binding
still requires inspecting actual hosted contents; releaseReady stays false.
PR21 documentation head 8a105c4 passes full push/PR runs 39398173948/39398182802.
The populated-list layout passes at baf6773; its long-Practice/indicator follow-up
at ef9d446 is currently under failure inspection and not accepted.

## Actual Windows metadata evidence

PR23 code db871eb passes full Linux/Windows push/PR runs 39399260940/39399290932.
Downloaded materials under .deps/windows-metadata-db871eb contain 158 retained
metadata files (186,979 bytes), all verified against the audit hashes. Hunspell
staged DLL, frozen recipe/patch, source archive and ABI package ID match its
retained SPDX/ABI records; see docs/evidence/windows-hunspell-cache-db871eb.json.
This is recorded cache correspondence, not an independent reproducible build.
Qt/graphics/VC runtime and declared libiconv dependency material remain open.
Independent static review found no important implementation issue.
