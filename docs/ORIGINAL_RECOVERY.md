# Original-text recovery when History is off

The approved contract says that an unsafe Undo must leave the original available
for explicit copying. Today a stale native Undo directs users to History even
when it is disabled, and the workspace retains its old original only in an
unreachable Undo buffer after editing. Fcitx does not forward confirmed passages
to the main process when History is off.

Keep exactly the latest confirmed passage pair in the running app's memory.
Cap each side at 50,000 Unicode characters (and reject oversized UTF-16 early).
Replace that single pair only after a confirmed passage operation, and provide
Copy original and Clear in the workspace/History controls. Everyday corrections,
errors, cancelled operations and unsafe fields never enter this cache. Clearing
saved History and clearing this session's recovery are distinct explicit actions.
Closing the app discards it. Do not promise protection from operating-system swap.

Use Controller delivery for the internal workspace and native Service results.
For the separate Wayland addon, send confirmed passages to a dedicated same-user
local socket. Reuse the existing account-private data-directory identity to name
it; do not extend the activation command parser or touch Clipboard automatically.
Limit frame size, simultaneous clients and lifetime; malformed/oversized frames
must be rejected atomically. No partial result, disk mailbox or user-text logging.
If the app is absent, do not retain a durable mailbox or silently enable History.

Verification: first reproduce missing copy after a workspace correction with
History off and subsequent edits. Test replacement/clear/size bounds and native
Service delivery. Add framed IPC tests for partial/oversized/malformed input,
client limits and timeout, then a real GNOME/Fcitx confirmed-passage test with
History off. Review text/IPC privacy and keyboard/UI behavior before integration.

## Implemented first phase

Controller holds one bounded pair independently of History. The workspace records
only after applying a matching result; Service emits passageApplied after its
native adapter confirms replacement and before optional history storage. Copy
requires an explicit button; Clear clears the cache and workspace Undo pair.
Oversized/unchanged pairs leave the previous recovery intact. A reopened
Controller has no recovery and disabled History stays empty.

The limit audit found an existing discrepancy: Store, Engine and Settings
allowed 100,000 characters despite the approved hard 50,000 ceiling. A shared
MaxPassageCharacters now bounds both input and output; UI cannot raise it.
UI-setting and direct-Engine oversized-option tests were observed failing before
the fix. Session copy, bounds/restart and native-Service signal tests also failed
before implementation, then passed. All six suites pass (17.86 seconds).

Independent read-only review found no Important issue in this phase. Synthetic
Overview/History captures were inspected in light/dark at minimum 880x620 logical
size with 150% scaling; controls are readable. New Windows CI has not run yet.
The Fcitx IPC path is deliberately still incomplete and this branch must remain
a draft until it and its security/native tests are implemented.

## Next phase: local channel and lifecycle

A separate bounded channel is still required for the out-of-process addon.
Use a fixed versioned frame format with UTF-16 units and validated lengths before
allocating, preserving the exact original without JSON Unicode loss. Each side
is bounded by 50,000 Unicode characters and 100,000 UTF-16 units. Reject invalid
magic, excessive lengths, truncated/extra data and incomplete client frames.
Restrict the socket to the account, bound active clients and frame deadlines,
and never use a disk mailbox or raw-text diagnostics. Only confirmed passage
pairs may be sent; word-level edits and cancelled/unsafe operations must not.

Lifecycle audit also found that Controller::quit only stops the UI process;
the Fcitx addon currently relies on stored settings and can continue correcting.
Tie addon permission to a live account-private application channel, including
fail-closed disconnect handling, so Quit/crash stops addon work. A persistent
handshake/acknowledged channel can provide that lifecycle and deliver the latest
bounded pair; it needs bounded pending data and no blocking socket waits on the
input path. Qt event-loop integration in the Fcitx host must be explicit and
covered by the real private GNOME probe. Recheck permission/lock before every
confirmation-to-recovery send. Update the probe to keep the actual app running,
verify History-off delivery, then verify no correction after the app exits.
