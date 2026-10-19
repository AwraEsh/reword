# Hosted packaged workload execution plan

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
