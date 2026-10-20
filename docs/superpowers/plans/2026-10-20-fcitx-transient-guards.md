# Fcitx transient-state guard implementation plan

> Execute inline with superpowers:executing-plans; use hosted CI for all C++ work.

**Goal:** apply the approved unsafe-field buffer and cancellation requirements to
the independent Wayland addon, without treating Service tests as addon coverage.

**Architecture:** retain the existing Fcitx event integration. Consolidate unsafe
observation invalidation only after reproducing retained job text. A worker's
cooperative cancellation remains distinct from clearing the owner's job record.

**Spec:** docs/REQUIREMENTS.md, privacy and selection-operation sections.
**Files:** src/platform/fcitx_addon.cpp; tests/fcitx_addon_test.cpp; CMakeLists.txt;
this record and docs/CURRENT_HANDOFF.md. No new runtime dependency.

- [ ] Add a real Fcitx-core/RecoveryServer/Store fixture using temporary data.
  A friend peer may inspect owned text, matching the Service privacy tests; do
  not add production test-only cleanup methods. Exercise password, sensitive,
  absent surrounding-text capability, denied permission, Pause and focus loss.
  Seed only synthetic pending/job text. Require cancellation and immediate
  release of owner-held text on the actual unsafe event paths. Positive control:
  the same focused permitted context is authorized before each unsafe change.
- [ ] Observe hosted RED before changing production behavior. A compile/setup
  error is not RED; repair the fixture separately until assertions run.
- [ ] Centralize the demonstrated invalidation boundary and use it on unsafe
  observation, focus/session loss, invalid text and exceptions. Keep confirmed
  recovery/history separate and do not erase user-authorized saved originals.
- [ ] Run the complete hosted Linux/Windows workflow; inspect exact results,
  request independent static review and record evidence. No local GUI/compiler.

Review focus: callback ordering on focus loss; retained asynchronous job text;
worker cancellation not being reset while it still runs; unsafe observations
never confirming history/statistics; fail-closed session/lock behavior. A later
deadline audit must cover addon acceptance/worker/completion, not only Engine's
own timer. Broader GNOME/application compatibility remains a separate gate.

## Paused checkpoint

The user requested a temporary pause. Test-only source 104482e failed compilation
in run 39482104028 because CapabilityFlag enums require an explicit flags wrapper
for operator|. Fixture follow-up f478026 is pushed. No behavioral RED has yet
been verified, and no production guard change was made. Inspect that run before
continuing; a compile error does not establish the suspected retention defect.
