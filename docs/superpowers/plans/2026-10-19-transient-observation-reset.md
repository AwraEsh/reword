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

- [ ] Add Service regressions: correction then stop/start must restart the
  first/second/third cycle; an adapter inspection exception after a correction
  must invalidate Undo so a later action cannot resurrect uncertain state.
- [ ] Observe those failures in hosted CI before changing production behavior.
- [ ] Implement private resetRetry()/resetObservation() helpers; audit every
  reset, policy/identity/caret/continuation/exception/Pause/stop path. Clear all
  retained QString references and reset related numeric/boolean state together.
- [ ] Verify existing alternating, held/overlapping shortcuts, safe boundary,
  selection/Undo/cancellation and full Linux/Windows tests remotely.
- [ ] Independent review, Git checkpoint/PR and authoritative handoff.

## Task 2: Native job cancellation audit

- [ ] Inspect owned worker captures and unsafe-field transitions. Determine
  whether a native job can retain prior permitted input until its deadline.
- [ ] If confirmed, reproduce with an owned native job and unsafe metadata only;
  ensure no password text enters processing. Cancel only stale native jobs while
  preserving explicit workspace jobs and the existing result/Undo safeguards.
- [ ] Review and remote verification before accepting the privacy requirement.

This plan records an open release gate. It does not establish password capture,
persisted text leakage or completion of the full buffer audit.
