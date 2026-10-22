# Fcitx operation status and GUI Cancel

Fcitx selected-text correction must expose the same visible Busy and Cancel
behavior as the native Service path. Reuse the authenticated account-local
recovery socket. Lifecycle messages contain no user text and never make a
successful operation eligible for persistent history.

- [ ] Add RED recovery-channel coverage for begin/end aggregation, cancellation,
  disconnect cleanup, coalesced fixed frames and zero passage delivery.
- [ ] Add RED Controller/QML coverage: an Fcitx lifecycle message shows the real
  workspace Cancel control; clicking it reaches the client; Busy remains until
  the client ends the operation.
- [ ] Add RED owned Fcitx coverage: the accepted selection announces Busy,
  server cancellation reaches the worker, produces no native write/history and
  clears Busy only after the worker is reaped.
- [ ] Extend the framed parser with bounded four-byte lifecycle commands. Track
  active state per authenticated peer, aggregate it in RecoveryServer, and send
  cancellation only to active peers. Disconnect and stop must clear aggregate
  state. Preserve exact UTF-16 recovery frames and their acknowledgement order.
- [ ] Have RewordAddon begin lifecycle only after field/size/operation-lock
  acceptance. Retain Busy while its worker owns the operation, including
  cooperative cancellation. End on every terminal path, recovery refusal,
  invalidated ACK or native commit. A remote Cancel must invalidate owner text
  immediately while the worker remains owned until reaped.
- [ ] Wire RecoveryServer activity into Controller::busy() and route the existing
  Cancel action to both Service and active Fcitx peers. Verify focused/full local
  tests within host limits, independent review, and Linux/Windows CI.

The protocol must not accept an unauthenticated peer, add network access, expose
passage text in status messages, or claim ordinary GNOME Wayland acceptance from
owned fixtures. Cancellation is cooperative; the UI remains Busy until the
worker reaches a terminal result.

## Review checkpoint, 2026-10-22

Implementation fd31520 builds locally and passes all 12 registered CTest targets
in 182.13 seconds with bounded serial software rendering. This is not acceptance:
independent review found two important cancellation races. Coalesced OKAY/CNCL
can deliver and commit before processing cancellation; an unscoped delayed CNCL
can cancel the next operation. The branch has not been pushed.

The real local socket regression cancellationPrecedesCoalescedDelivery reproduces
the first finding: two setup/cleanup cases pass, the regression fails because only
the delivery callback runs (expected cancellation before delivery). No skips.
Keep this RED until the protocol gives each operation an epoch and validates
cancellation before authorizing a write. Also test delayed cancellation across
consecutive operations and the actual addon no-write/history invariant.

Both findings now have observed local REDs: coalesced delivery loses Cancel;
endingOperationMustDrainDelayedCancellationBeforeRestart fails because ready()
returns true before the server confirms the operation has ended. The working
fix uses ordered end acknowledgement (DONE) as a quiescence barrier rather than
operation epochs: ready()/beginOperation refuse until DONE, which follows every
server-side cancellation for the ended operation on the same stream. Late Cancel
while ending is ignored. Client delivery callbacks run only after parsing all
available response tokens, including waiting for a fragmented trailing token.
The recovery suite passes in 6.48 seconds, including coalesced and fragmented
Cancel and the consecutive-operation barrier regression. Addon/UI relink and
focused verification subsequently pass at b603bed: recovery 6.52 s,
fcitx_state 17.22 s, ui 12.64 s; total 36.39 s, zero failed targets.
Full verification and renewed independent review remain pending. The existing
reviewer has been asked to inspect fd31520..b603bed without builds or edits.
This revision has not been pushed.

Follow-up review found that the first barrier guarded restart only: an exact
four-byte OKAY read still authorized writing before Cancel in a later read.
deliveryWaitsForServerBarrierAcrossSeparateReads reproduces that RED (delivery
count 1, expected 0). The revised client requests operation closure after OKAY,
continues accepting cancellation while delivery is pending, and emits delivered
only after ordered DONE. Recovery passes in 6.71 seconds with this revision.
The UI fixture also waits for client.ready() before a consecutive operation,
since server Busy clearing precedes DONE reception. Addon/UI relink and focused
tests are running; this revision still needs full verification and review.
The preceding source did pass all 12 tests in 86.96 seconds; that result does
not cover this later barrier change.
