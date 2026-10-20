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

- [x] Add Service regressions: stop and unsafe metadata must release transient
  text; an adapter inspection exception after a correction must invalidate Undo
  so a later action cannot resurrect uncertain state. A narrow friend test peer
  returns only a text-retention boolean, never contents or a production API.
- [x] Observe those failures in hosted CI before changing production behavior.
- [x] Implement private resetRetry()/resetObservation() helpers; audit every
  reset, policy/identity/caret/continuation/exception/Pause/stop path. Clear all
  retained QString references and reset related numeric/boolean state together.
- [x] Verify existing alternating, held/overlapping shortcuts, safe boundary,
  selection/Undo/cancellation and full Linux/Windows tests remotely.
- [x] Independent review, Git checkpoint/PR and authoritative handoff.

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

Resumed review found no important production reset regression and identified
the exception-row polling race. Diagnostic run39391142137 at633e9ed confirms
the only Windows failure is unsafeBoundaryReleasesTransientText(exception).
Its JUnit is retained under .deps/transient-reset-633e9ed-windows. Quiesce
the synthetic adapter inside the throwing callback so a later permitted tick
cannot repopulate the observation before the assertion. The original code
still fails because its retained retry/Undo references are not cleared. Fresh
hosted verification is required; this change neither relaxes the assertion nor
alters production exception handling.

Task2 regressions add a one-shot native-write callback, then change application,
field identity, password role or metadata certainty. Live/selection/Undo paths
must not request any follow-up text from that changed target. Selected original
remains available in session recovery, but uncertain results are not history.
An unconfirmed selected write must also retain the original for copying. Observe
hosted RED before changing read/recovery behavior. No real desktop is exercised.

Run39392033809 atcaea11f reproduces all12 post-write target-change cases plus
missing recovery after an unconfirmed selection on Linux; Windows Service also
fails. Log .deps/post-write-red-caea11f.log. Task1's exact reset revision2d88084
passes both platforms in39391695577 after fixing the exception-fixture race.

Post-write follow-up now peeks metadata and requires the expected app/field
before permission checks and any text read. Live/selected/Undo snapshots are
accepted only when the guarded result matches the intended text/caret; unsafe
or mismatched results clear observation and never become selected history.
Selected original/result recovery is prepared before the write attempt via
passageRecoveryAvailable (renamed to avoid claiming an unconfirmed edit applied).
Controller session recovery remains bounded and history-independent. No new
clipboard access, persistence or logging. The metadata-to-text adapter identity
gap is still open; do not label the complete read-boundary audit accepted.

Independent Task2 review found a cancellation gap after recovery preparation:
passageRecoveryAvailable can deliver a direct callback after the last policy
check and before replace. Add five policy-change regressions (pause, deny,
language revision, cancel, cancel-then-prepare), observe hosted RED, and recheck
policy after recovery preparation immediately before writing. The original
remains copyable; recovery availability must never imply a completed edit.

Expected-target read contract (remaining Task2 scope):

- Add Adapter::readExpected(const Field&) with a fail-closed default. Service
  passes its authorized metadata; app and field ID must match before text is
  requested. Keep inspect(false) metadata-only. Existing owned observer probes
  may retain inspect(true), but product authorization uses readExpected.
- LinuxProxy sends only expected app/ID in a dedicated bounded read request.
  Helper rejects a missing identity. LinuxAdapter pins one referenced AT-SPI
  object before metadata calls, checks its expected identity before text calls,
  and uses that same validated object for writes. Focus events cannot redirect
  intermediate calls through mutable focus_. Native pre/post-write reads use
  the expected identity too.
- Windows pins its existing local UI Automation element; compare app/ID before
  nativeSnapshot/GetText, and use guarded reads around replace. Preserve all
  password, lock, editable, plain-field and size checks.
- Reproduce metadata-to-read focus change with an owned adapter callback after
  metadata capture. Assert no text read from the changed field and no writes,
  history or retained observation. Verify owned Linux/Windows native probes,
  full hosted suites and an independent review before accepting this boundary.

No readExpected implementation or native-job cancellation is accepted yet.

Task1 accepted at2d88084: full Linux/Windows run39391695577 passes,
independent static review found no important production reset issue. Partial
Task2 at58959a8 passes full Linux/Windows run39393054682, including the13
post-write/recovery regressions. This does not cover the adapter read-identity
gap or cancellation during recovery signal delivery. Regression commit4853ba6
is running in39393873509; production behavior must wait for its observed RED.

Hosted39393873509 at4853ba6 reproduces all five recovery-policy cases:
Linux writes once when zero writes are required (62 other cases pass); Windows
Service also fails. Saved .deps/recovery-policy-red-4853ba6.log. Recheck policy
immediately after signal delivery, clear observation on refusal and retain the
explicit session original. Hosted GREEN remains required.

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

Task3 source audit confirms unsafe tick paths reset observation but do not
request cancellation of a running native selected job. Add a controlled worker
pool blocker in Service tests so unsafe metadata arrives before computation;
always release the blocker before Service teardown, including assertion exits.
Require cancellation for password,unknown,locked,denied,paused,changed app/field
and metadata exceptions, with no extra text reads/writes/history. An independent
workspace job must still complete while an external password field is focused.
Use a test-only boolean cancellation observation, never user contents. Observe
hosted RED before adding native-job identity tracking and cancellation. Running
worker cancellation remains cooperative; do not claim QString zeroization or
immediate destruction of copies already captured by QtConcurrent.

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

## Operation deadline follow-up

Source inspection shows the ten-second timer starts inside the worker, so a
queued job receives a fresh full budget after waiting. Add a real queue-delay
regression using the controlled pool blocker: wait 10.1 seconds on hosted CI,
release the worker and require the unchanged original with a timeout error.
Do not shorten or inject the production limit merely to make the test faster.
Observe RED before starting the deadline at request acceptance and checking
it before worker initialization and again before delivering/applying a result.
This is separate from secure memory erasure and normal desktop acceptance.

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
