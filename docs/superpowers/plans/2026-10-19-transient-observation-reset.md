# Transient observation reset plan

> Use executing-plans inline after the current active-workload follow-up review.

**Goal:** Release transient live/retry/Undo text references at unsafe boundaries,
exceptions and Service shutdown, without erasing explicitly requested history or
session recovery.

**Spec:** REQUIREMENTS.md temporary-buffer/privacy and same-location retry rules.

**Architecture:** Centralize retry reset (RetryCycle, location, prefix and typo)
and observation reset (previous field, Undo and retry). Reuse the helpers at
existing reset sites. Do not claim secure memory zeroization: QString may share
storage. Native in-flight cancellation needs a separate explicit audit; an
internal workspace operation must not be cancelled merely because its GUI's
external correction permission is disabled.

**Files:** src/app/service.h, src/app/service.cpp, tests/service_test.cpp,
docs/CURRENT_HANDOFF.md.

## Constraints and review focus

- Never read password/unknown field text to test or clear a boundary.
- Do not break passive-shortcut focus-loss handling or same-location alternation.
- Preserve the user's intentional bounded history/session recovery controls.
- Cancellation stays cooperative and results remain guarded by field/selection,
  policy and operation revisions; never share live/passage Engine instances.
- No local build/GUI/heavy tests after hard freezes. Observe C++ RED/GREEN in
  hosted CI and retain exact revisions; tiny Python fixtures remain bounded.

## Task 1: Reset ownership and behavior

- [ ] Add Service regressions: stop and unsafe metadata must release transient
  text; an adapter inspection exception after a correction must invalidate Undo
  so a later action cannot resurrect uncertain state. A narrow friend test peer
  returns only a text-retention boolean, never contents or a production API.
- [ ] Observe those failures in hosted CI before changing production behavior.
- [ ] Implement private resetRetry()/resetObservation() helpers; audit every
  reset, policy/identity/caret/continuation/exception/Pause/stop path. Clear all
  retained QString references and reset related numeric/boolean state together.
- [ ] Verify existing alternating, held/overlapping shortcuts, safe boundary,
  selection/Undo/cancellation and full Linux/Windows tests remotely.
- [ ] Independent review, Git checkpoint/PR and authoritative handoff.

## Task 2: Post-write read authorization

- [ ] Reproduce focus/permission changes immediately after native live,
  selection and Undo writes using a one-shot synthetic adapter callback.
  No follow-up text read may target the newly blocked application.
- [ ] Replace unguarded post-write inspect(true) calls with policy/metadata
  authorization. Preserve the original for explicitly requested selection
  recovery even when confirmation/focus is lost; never save uncertain edits
  as confirmed history or retain newly blocked text in observation buffers.
- [ ] Audit the metadata/read identity gap separately: inspecting a fresh field
  after authorized metadata must not read text from a different target before
  discovering that its ID changed. Define an expected-target read contract if
  existing adapters cannot enforce it. Retain password/unknown fail-closed rules.
- [ ] Hosted RED/GREEN, native regressions and independent review.

## Task 3: Native job cancellation audit

- [ ] Inspect owned worker captures and unsafe-field transitions. Determine
  whether a native job can retain prior permitted input until its deadline.
- [ ] If confirmed, reproduce with an owned native job and unsafe metadata only;
  ensure no password text enters processing. Cancel only stale native jobs while
  preserving explicit workspace jobs and the existing result/Undo safeguards.
- [ ] Review and remote verification before accepting the privacy requirement.

This plan records an open release gate. It does not establish password capture,
persisted text leakage or completion of the full buffer audit.

Execution ruling: reuse the clean managed storage-access worktree on
codex/transient-observation-reset, stacked on8570675 while PR20's hosted soak
runs independently. Add regressions and the friend declaration before behavior
changes, push for hosted RED, then implement the reset helpers. stop() destroys
its injected adapter; testing restart with that destroyed fixture would be
invalid, so test stop's memory-release contract directly and keep public Undo
failure coverage. No local C++ build/GUI is allowed.

Hosted RED39321745068 at99f91d3 compiles both platforms, then fails the Service
suite as expected: seven text-retention boundaries and stale Undo after an
inspection exception fail on Linux; Windows Service suite also fails. Saved
log .deps/transient-reset-red-99f91d3.log. Implement central reset helpers and
preserve explicit history/session recovery; post-fix remote verification pending.

Source inspection also found direct post-write inspect(true) calls in live,
selected and Undo paths, without the permission checks used by permitted().
Task2 records this separate boundary audit; no behavioral exploit or password
capture is claimed before its synthetic regressions run. Native worker capture
cancellation remains a separate open task. Do not call the full privacy gate
complete merely because the first reset regressions pass.

Post-fix d27d3f8/run39322671718 passes Linux completely but fails the Windows
Service suite. Windows CTest captures no QtTest stdout (even passing suites have
empty output), so the case is unknown. Add JUnit file output to the Service
test and retain it with the existing diagnostics. Do not guess a production
fix from the suite exit code. Independent review was interrupted by the agent's
account usage limit and remains incomplete, not approved.
