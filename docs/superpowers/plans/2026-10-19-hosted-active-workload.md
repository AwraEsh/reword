# Hosted packaged workload execution plan

## Long-run failure investigation — 2026-10-21

Actual run 39394646024 at 0fd1f2e failed after 1,819.969 seconds, with
native-text-mismatch during live typing. It completed 632 sustained cycles;
live-first/second/third counts were 634/633/633, identifying the second-attempt
assertion as the failing boundary. Target 7,200 seconds was not completed.
Helper shutdown required escalation. Its first two-minute preflight passed.
The retained artifact is ignored under .deps/hosted-active-2h-0fd1f2e.

The content-free report did not distinguish a corrected second attempt from
missing/extra edits. Do not guess a core fix or relax any assertion/budget.
The isolated codex/active-workload-diagnostics branch adds per-attempt stage,
monotonic observation times, a fixed synthetic state classification, change
count and at most 12 numeric event records. Unknown text becomes only `other`
and character count; content never enters the report. Normal production code,
timing delays, pass criteria and cleanup escalation are unchanged.

Two small classification/privacy tests pass under 256 MiB/10 CPU seconds,
alongside all 18 existing active-stability fixtures. Hosted full-suite and
same-package preflight must pass before a new instrumented long run. These
diagnostics gather missing evidence; they do not fix or accept the failed run.

> Use executing-plans inline. This extends the existing active-stability plan's
> execution location while local builds and GUI workloads remain suspended.

**Goal:** Obtain real packaged-GUI correctness/resource evidence on a hosted
Linux runner without risking another build or GUI run on the user's machine.

**Architecture:** Add a manual input to the existing build workflow. After its
normal tests/native probes, explicitly opted-in runs stage the application and
invoke the existing owned-desktop workload runner. Always retain its numeric
report, including failures. The normal push/PR workflow stays short.

**Spec:** REQUIREMENTS.md and 2026-10-18-active-stability.md. This supplements
the current-host latency and final-package gates; it cannot replace either.

**Files:** .github/workflows/build.yml; docs/CURRENT_HANDOFF.md.

**Interface:** workflow_dispatch input active_seconds, choice 0/120/600/7200,
default0. Only a nonzero manual dispatch stages dist/Reword.AppDir and runs
test-native-linux.py --active-app dist/Reword.AppDir/AppRun --active-seconds
<choice> --active-output dist/active-workload. Job timeout180minutes.

## Constraints and review focus

- Keep all existing budgets, synthetic fields, password checks and cleanup.
- Never use the host user's display, clipboard or personal application.
- Failed/no report must never become acceptance; retain reports with always().
- Report Ubuntu runner evidence honestly; current Debian-host and ordinary
  Windows desktop acceptance remain open.
- No installer/product publication or local installation; source/notices and
  final binary acceptance remain separate gates.

## Task 1: Dispatch, stage and retain

- [ ] Add typed manual input and conditional stage/active/report steps. No new
  workload implementation or tests mirroring YAML; use real hosted execution.
- [ ] Run existing Linux/Windows workflow plus manual120-second real packaged
  workload. Inspect actual report, failure classification, binary hashes and
  identities; fix demonstrated defects without weakening budgets.
- [ ] Independent review of workflow and evidence; commit/PR/native attachment.

## Task 2: Longer execution

- [ ] Once the short run passes, dispatch7200seconds at the unchanged revision.
- [ ] Inspect duration, latency, correctness and app/helper memory/CPU/swap
  counters, normal cleanup and hashes. Retain failure evidence if it fails.
- [ ] Record exact run/source/artifact identities and remaining acceptance gates.

Execution ruling: the user explicitly authorized unattended progress. Implement
inline and obtain review; do not reopen routine implementation choices while
  they sleep. Preserve the local-heavy-work suspension and shutdown condition.

First hosted run 39315576445 at 9aee694 passed the existing Linux tests and all
three native cycles, then refused staging because a scanner module path/formal
identity did not match. No active workload began and no soak is accepted.
Retain the source-only QML scan JSON on the next run to diagnose the actual
Ubuntu SDK layout before changing any validation. This contains build metadata,
not application/user text. Runtime reports remain separate when they exist.

Run 39316211634 retained the real scan. QtQml resolves to its parent directory
but has no plugin metadata; the install list contains WorkerScript, not the
QtQml module package. The official Ubuntu qml6-module-qtqml 6.4.2+dfsg-4build3
archive was inspected without installation/execution: it owns QtQml/qmldir,
libqmlplugin.so and imports QtQml.Models/WorkerScript. Add those missing module
packages plus QtQuick.Shapes (unresolved in the actual scan). Keep the existing
path/formal-identity validation. No local SDK/compiler/GUI was executed.

Run 39316879658 at 56138e6 successfully stages the real AppDir and executes the
workload. It fails the unchanged resource budget: app15.90% of one core after
30.13 sustained seconds, helper 2.51%, zero swap, normal cleanup. Word 23 samples
p95 upper bound 22.3ms/max 25.261ms. It completed 110 initial history edits and 10
full cycles before resource rejection, not the requested 120 seconds or final
oversized-selection checks. The failed baseline does not justify a long soak yet.

Before optimization, add an explicit diagnostic mode to the existing performance
probe: compare 10 fresh Engine construction+short passage+destruction operations
with 10 short passages on one Engine, retaining CPU and wall totals and exact
synthetic correctness only. Service currently constructs a new Hunspell-backed
Engine for every selected correction. This is a hypothesis until measured.
Run the diagnostic only on hosted manual executions; do not change production
code or resource/latency budgets based on this hypothesis alone.

Measured run 39318112831: ten fresh engines/passages/destructions use 667.6  CPU ms,
ten passages on a reused engine use 0.6  CPU ms; exact synthetic results agree.
The full workload reaches all 120seconds and limit checks but fails normal
cleanup, so it remains rejected. App sustained CPU 9.08%, helper 1.57%; word p95
22.7ms. Keep this second baseline alongside the earlier CPU rejection.

### Task 1 follow-up: measured initialization cost and cleanup diagnosis

- [ ] Reuse a separate passage Engine owned by Service, initialized lazily only
  inside its existing single in-flight worker. Never share the live Engine.
  Stop waits for the job before destroying it; exceptions discard the worker
  Engine. Refresh personal words/replacements and options on every operation.
- [ ] Add a sequential-job regression covering add/remove personal words,
  add/remove custom replacements and language switches so reuse cannot retain
  stale user configuration. Verify on hosted CTest, never local compilation.
- [ ] Add fixed fixture/app/helper forced-cleanup roles to the numeric report;
  pin the existing stubborn-child test before implementation. Keep deadlines,
  escalation behavior and failure propagation unchanged until the actual role
  and cause of forced shutdown are known.
- [ ] Run 120seconds at the changed source; inspect all limits, hashes and cleanup.
  Only a fully passing short run permits advancing to the unchanged 7200-second
  workload. No product acceptance is inferred from diagnostic microbenchmarks.

Tiny harness tests exposed delayed SQLite connection cleanup: Python's
connection context commits/rolls back but does not close. A regression retained
the connection and failed before explicit closing; success and failure paths
now close it. Seed/settings readers and synthetic test setup also close their
connections deterministically. Forced-role reporting was RED before adding the
fixed role list. All 18 tiny harness tests pass in 0.365 s under 256 MiB and
10 CPU seconds. C++ service changes require hosted compilation/tests.

Run 39319638562 at 31ab2cd passes the full Linux/Windows workflow and the
120-second packaged run (149.621 s including setup/burst/cleanup). It completes
44 full cycles, all history/selection-limit checks and normal cleanup. Word
90 samples: p95 upper bound 21.8 ms, max 22.505 ms. Sustained app CPU 6.64%,
helper 1.06%; zero swap, private growth 1,316/1,224 KiB. App SHA256
0c3aaade2fca52d92712efee130a1064e6002630bb6f8e9e2a8a7e810dcaf22c.
Independent review found no important lifecycle/concurrency issues. Follow-up
afa301b adds cancellation-then-next-passage coverage and passes full hosted
Linux/Windows runs 39320326333 and 39320337032. The earlier forced cleanup is not
proven fixed by engine reuse; the passing run records an empty forced-role list.

For a 7200-second dispatch, require an additional 120-second preflight on the
exact same staged AppDir immediately before the long run. Separate new owned
desktops, databases and report directories preserve isolation. A preflight
failure skips the long step; always retain both reports when present. This
avoids treating a prior build's short pass as evidence for a rebuilt candidate.
It remains hosted development evidence, not the final current-host/product soak.

Run 39321497233 at 8570675 correctly refuses the two-hour run: its short preflight
fails the unchanged CPU budget at app 14.11% after 30.13 s (helper 2.37%, no swap,
normal cleanup, word p95 bound 24.8 ms). Engine reuse alone is not sufficient on
every hosted runner. Retain .deps/hosted-active-8570675/active-preflight/report.json;
no 7200-second workload started.

Next collect numeric main-thread CPU alongside the existing aggregate counters
to distinguish UI work from worker/Service work before another optimization.
Main-thread reads use the already pinned PID/start identity and bounded procfs
reader; missing/reused/rollback data fails, never becomes zero. Reset both
baselines at the same post-burst boundary and retain burst measurements. No
content, thread names, command lines or stack arguments enter reports. No CPU,
latency or cleanup limit changes. Five tiny diagnostic tests are RED before the
module exists; hosted execution is still needed for actual attribution.

Actual diagnostic run39391593030 at7df1da1 fails appCPU13.50% after30.14s;
main thread accounts for10.65%, leaving about2.85% in other app threads.
Word p95 bound21.8ms, no forced cleanup or swap. Report is retained in
.deps/hosted-active-7df1da1/active-workload/report.json. This points the next
investigation at UI work, not another polling/engine change.

Add a counted Controller subclass only in the real QML UI test. One explicit
updated signal should read the complete settings map at most twice, rather
than once per independent binding. The same test must verify English switching
still updates the actual control both ways. Observe hosted RED before replacing
Main.qml/SettingRow.qml bindings with one shared root settings snapshot. No
production diagnostic API, persistent cache, rule, language or UI copy change.

Hosted RED39392713956 at859b133 confirms one backend notification reads the
full settings map37times in the actual QML interface. Each getter performs
many SQLite queries. Main.qml now owns one notification-driven settingsSnapshot
and passes it to every SettingRow; other bindings use that same snapshot.
This is scoped to rendering, not an authorization/configuration cache in the
correction service. The test still requires actual English toggle propagation.
Full remote UI/native/installer checks and new packaged CPU evidence are pending.

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
