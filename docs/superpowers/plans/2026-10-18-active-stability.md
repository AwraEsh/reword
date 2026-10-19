# Active stability evidence implementation plan

> For agentic workers: use executing-plans inline, task by task. Implementation stays in this chat; independent review follows each completed deliverable.

**Goal:** Exercise the packaged Reword GUI and native helper for several hours in an owned synthetic desktop, retaining content-free resource and correctness evidence.

**Architecture:** Extend the existing private Xvfb/D-Bus/AT-SPI runner with a specific active-app mode. A separate workload driver controls only the synthetic GTK fixture and private test database. A small Linux process sampler reports application/helper counters and rejects missing or reused process identities.

**Tech stack:** Python standard library, Linux /proc, existing Qt/GTK fixtures and owned X11 desktop.

**Spec:** docs/REQUIREMENTS.md; docs/COMPATIBILITY.md. This plan implements stability evidence, not additional application features.

## Global constraints

- No host display, user typing, clipboard, lock screen or personal app is exercised.
- Run the actual packaged GUI and its native helper; Service-only probes are separate evidence.
- Preserve first/second/third semantics, Pause, password exclusion, selected correction/Undo and the 100-record history bound.
- Reports contain hashes, counters, timing and fixed synthetic scenario IDs; no text payloads or private executable/window titles.
- Engine latency is not end-to-end word latency; report these separately. Target word p95 remains under 50 ms on the current system.
- Final release/install still require the complete product gates and normal Windows desktop evidence.

## Review focus

- PID reuse or helper restart must invalidate a continuous-process claim.
- An unreadable memory metric must fail collection, not become zero usage.
- Sampling must include helper CPU, private allocations and swap as well as app RSS.
- Fixture watchdog expiry and workload failure must not produce a successful soak result.
- A delayed input command must not be counted as correction-engine time.

### Task 1: Content-free Linux process metrics

Files: create scripts/process_metrics.py and tests/test_process_metrics.py.
Interface: sample_process(pid, proc_root=Path('/proc')) returns numeric counters including startTicks, cpuTicks, rssKiB, privateKiB, pssKiB, dataKiB, swapKiB, threads and descriptors. read_child_pids(pid, proc_root) enumerates children across all application threads. ProcessSeries.observe(sample, elapsedSeconds) rejects identity changes and decreasing counters, computes CPU seconds/percent and memory growth relative to the first sample.

- [x] Write and observe failing tests for real-format stat names containing spaces/parentheses, private totals, missing required fields, malformed/negative values, unreadable/missing files, multi-thread child enumeration, PID reuse and CPU/growth calculations.
- [x] Implement bounded reads and numeric-only reports; no process command line or environment inspection.
- [x] Run the tests and whole existing CTest suite, then independent review and commit. This task alone is not an active-soak pass.

### Task 2: Bounded fixture lifetime and monotonic change timing

Files: tests/native_fixture.cpp, controlled probe tests and runner.
Interface: explicit test-only lifetime bounded to 5 hours; optional fixed-format monotonic change records with sequence/timestamp only, separate from existing synthetic snapshot protocol.

- [x] Reproduce short watchdog expiry and missing timing evidence in controlled fixtures.
- [x] Add validated lifetime/timing options, retain existing defaults and native checks.
- [x] Verify first/second/third, password transition and native edit timestamps in the owned desktop; review and commit.

### Task 3: Packaged GUI workload runner

Files: scripts/test-native-linux.py, scripts/active_stability.py and tests/test_active_stability.py.
Interface: specific --active-app path, duration and report directory; no arbitrary command runner. The outer runner owns X authority, D-Bus, runtime/test database, children and cleanup. Start app before fixture; authorize only the fixture executable in the private database. Use real registered shortcuts for selected fix/Undo/Pause.

- [ ] Add failing tests for process loss, watchdog expiry, wrong synthetic result, history bound overflow, timeout/cancel, rejected metrics and report atomics.
- [ ] Drive live cycles, selected fix/Undo, Pause/resume, password transitions and 100+ history insertions; retain app/helper identities and bounded timing samples. Clean up only children/resources owned by this run.
- [ ] Run short integration baseline, report end-to-end p50/p95/max and resource deltas with explicit budgets. Investigate any latency failure before optimization; re-run native safety cases after changes.

### Task 4: Final package soak and acceptance record

- [ ] Build final candidate packages with matched sources/notices/checksums after UI gates pass.
- [ ] Run multi-hour active workload against the exact packaged binary, retain its SHA and both process series. Check correctness counters, limits, no swap/process restart and resource trends.
- [ ] Record measurements, failures and environment in compatibility/handoff docs. Rebuild invalidates final-binary acceptance. Keep release/install gated on the remaining Windows/native desktop checks.

## Task 1 evidence

Initial test collection was RED because the sampler module did not exist. All
13 functional/integration tests now pass, including the caller's actual Linux
process. Review found CPU rollback within one sample was accepted; its dedicated
regression was observed RED before preserving/comparing the first CPU count.
Independent follow-up confirms that fix and all 13 tests. Full local build and
all eight CTest suites pass in 53.05s. The sampler does not yet drive Reword or
produce an active-soak acceptance result. Tasks 2–4 remain open.

## Task 2 evidence

Invalid lifetime/timing inputs were RED before validation and now fail before
GTK initialization. The positive watchdog and quiet-mode probe pass. Missing
programmatic CHANGE records were reproduced before connecting the signal;
the first cold-start six-second test deadline was inadequate and is not app
failure evidence. The probe now allows bounded startup time separately from the
three-second fixture watchdog.

Review required actual native-edit timestamps, beyond stdin-generated changes.
The Service probe's new check failed without timing instrumentation, then passed
with it enabled: three owned native cycles verify first/third edits, retry,
Undo, Pause, selected history and password exclusion. Protected fix emits no
changes. Independent final review found no blocking issues. All nine final CTest
suites pass in 35.77s. This is instrumentation acceptance, not a latency or
packaged-GUI soak result. Tasks 3–4 remain open.

## Task 3 working evidence — resumed 2026-10-19

The user explicitly resumed after the paused checkpoint. Implementation remains
in the existing managed `codex/active-gui-workload` worktree. No development
package is installed or published as a product release.

The runner utilities have 17 passing tests under system Python 3.13, including
actual owned subprocesses, strict bounded framing, pidfd identity/loss detection,
bounded TERM/KILL cleanup, cancellation, atomic reports and exact history pairs.
New missing-cleanup/history/consent cases were observed RED before implementation.
Full CTest initially failed two pidfd cases because cached CMake selected a local
Python 3.11 build without pidfd APIs. System Python has both APIs; the harness
still fails closed if they are missing. Tests explicitly skip those two actual
pidfd cases on unsupported Python builds; acceptance runs require these APIs.

Short integration baselines 01–04 are failed evidence, never accepted soaks:
01 native mismatch/p95 56.7 ms (four samples); 02 first live timeout; 03 history
burst CPU budget; 04 Pause mismatch. Baseline 04 overlapped build/test work and
its latency is not an uncontended performance result. The GUI self-consent race
was addressed by seeding an explicit denial for the workload GUI only; the
synthetic fixture remains explicitly allowed. Hidden Qt menu/tooltip windows
were being closed instead of the normal application window; filtering the
normal window removed forced shutdown in baseline 04. Pause had no pipe/input
barrier: GTK could process its clear after XTest typing. A snapshot acknowledgment
now precedes typing. These changes need the next controlled integration rerun.

Ruling: retain the 110-edit history burst as separate resource evidence and
start sustained CPU/growth accounting after it. The burst drives corrections as
fast as they complete, unlike the paced live workload. Its high CPU remains
reported in `historyBurstResources`, not erased; absolute RSS and swap limits
apply throughout. Sustained CPU remains limited to 10% of one core after 30s,
private growth to 64 MiB, app/helper RSS to 512/256 MiB, swap to zero. Process
identity stays pinned across phase baselines. Cost if wrong: optimistic sustained
metrics; the retained burst evidence and final workload review must expose that.

History insertion now needs a newer record ID plus exact original/result pair,
even at the 100-record cap. Oversize acceptance requires a known correctable
49,998-character control (50,000 after correction), followed by its 50,001-
character counterpart with the same correctable prefix. Successful role samples
are counted only after collection; final acceptance forces fresh role samples
and cancellation checks. Task 3 remains open pending integration and review.
