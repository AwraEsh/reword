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

- [ ] Reproduce short watchdog expiry and missing timing evidence in controlled fixtures.
- [ ] Add validated lifetime/timing options, retain existing defaults and native checks.
- [ ] Verify first/second/third, password transition and native edit timestamps in the owned desktop; review and commit.

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
