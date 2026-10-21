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

- [x] Add a real Fcitx-core/RecoveryServer/Store fixture using temporary data.
  A friend peer may inspect owned text, matching the Service privacy tests; do
  not add production test-only cleanup methods. Exercise password, sensitive,
  absent surrounding-text capability, denied permission, Pause and focus loss.
  Seed only synthetic pending/job text. Require cancellation and immediate
  release of owner-held text on the actual unsafe event paths. Positive control:
  the same focused permitted context is authorized before each unsafe change.
- [x] Observe hosted RED before changing production behavior. A compile/setup
  error is not RED; repair the fixture separately until assertions run.
- [x] Centralize the demonstrated invalidation boundary and use it on unsafe
  observation, focus/session loss, invalid text and exceptions. Keep confirmed
  recovery/history separate and do not erase user-authorized saved originals.
- [ ] Cover timer-only Pause/denial/lock changes while a future is outstanding;
  observe RED, then cancel and release owner state without waiting for completion.
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

## Resumed — 2026-10-21

Run 39482540531 at f478026 compiles, then aborts before assertions because
inputStateFactory is not registered. The fixture omitted Instance::initialize().
The upstream 5.1.7 [implementation](https://github.com/fcitx/fcitx5/blob/5.1.7/src/lib/fcitx/instance.cpp)
registers the property there. Initialize the owned instance with all addons
disabled; keep its Fcitx config path temporary. This is fixture repair, not a
behavioral RED or production fix. The other 11 Linux suites passed in that run.

Hosted RED 39538586697 at d1e70c7 executes all six cases and fails their owner
text retention assertion: password, sensitive, no surrounding-text capability,
denied permission, paused and focus loss. Cancellation and pending reset already
pass; job_.before/after/original/corrected remain populated. The other 11 Linux
suites pass. Log retained under ignored .deps/fcitx-d1e70c7.log.

Consolidate existing unsafe resets so they cancel and clear job/pending text,
retry prefix, observation metadata and deletion correlation together. Preserve
the future's lifetime; cancellation is cooperative and worker copies can outlive
the owner record until the worker returns. Persisted recovery/history are not
modified. Hosted GREEN and independent review are pending. Fixture follow-up
also resets GNU getopt between instances (logs showed only the first instance
received --disable all) and retains one temporary Fcitx config path for all rows.

Static review of d1e70c7..7ad9a99 found no important issue in the scoped reset.
It explicitly leaves worker-cancellation timing and timer-only policy changes
open. Add a real private D-Bus lock provider and require the safe baseline to
be unlocked. With an outstanding promise-backed future and no pending Undo,
change Pause, permission or lock state without input events, then run the actual
Fcitx timer for 160 ms. Require cancellation and empty owner text before allowing
the future to finish. This should expose the timer's current early return while
the future is not ready. Observe hosted assertions before changing that policy.

The scoped reset at 7ad9a99 passes full Linux/Windows run 39539160624. It does
not close timer-only policy or deadline coverage. Follow-up fixture 575b4d6 uses
dbus-run-session (test-only host tooling already present in CI) and a local Qt
lock object; run 39539553700 is pending. PR25 is a draft stacked on PR23.
