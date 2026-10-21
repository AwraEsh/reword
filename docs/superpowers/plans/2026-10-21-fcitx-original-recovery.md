# Fcitx original recovery before write

The selected original must already be copyable when a native write happens,
including when the frontend never confirms the write. Reuse authenticated
same-account RecoveryClient/Server and their existing acknowledgement; do not
add network access, clipboard fallback or a blocking wait on the input loop.

- [x] Real owned Fcitx/Store/recovery regression: accept the shortcut and lock,
  substitute the worker-result boundary, and observe native commit ordering.
  Original/result must reach recovery before commit. Disconnect, permission,
  Pause, changed selection and expiry after receipt must prevent a write.
  History stays empty without confirmation and inserts once after confirmation.
  Observe local RED under one-job resource bounds.
- [ ] Hold the prepared job and operation lock while awaiting recovery ACK.
  Accept a selection only when the client has no prior queued delivery. Recheck
  policy, identity, selection, cancellation and the same acceptance deadline
  after ACK before committing. Ignore stale ACKs after cancellation and keep
  futures owned until reaped. No duplicate submission at confirmation.
- [ ] Verify local focused/full tests within safe bounds, independent review,
  and required Windows CI without repeated large artifact downloads. Record
  exact source and checks; ordinary Wayland and GUI IPC Cancel remain separate.

Design choice: waiting for the existing client ACK proves the GUI-side passage
receiver ran before write. Merely submitting then committing cannot prove this.
Restricting this addon to one delivery until ACK avoids correlating an old ACK
with a new job without changing the established wire protocol. Generic recovery
clients retain their existing bounded queue behavior. Cancellation after receipt
may leave an explicit session-recovery original, but never persistent history
for an unconfirmed write.

Local RED: the six new rows fail as expected on the actual accepted shortcut
path. The normal control writes before recovery delivery; disconnect/deny/Pause/
selection/expiry controls also write. Actual JUnit: 8 cases including init/cleanup,
6 failures, zero errors/skips, 2.901 s; ignored receipt .deps/fcitx-recovery-red.xml.
Built serially under 1 GiB address-space/90 CPU seconds with existing dependencies,
ran under 512 MiB/20 CPU seconds in a private D-Bus session. Core also passes.
Fcitx reports disabled keyboard/input-method addons because the fixture deliberately
has no real desktop frontend; these warnings do not skip assertions.
