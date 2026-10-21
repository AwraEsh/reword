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
