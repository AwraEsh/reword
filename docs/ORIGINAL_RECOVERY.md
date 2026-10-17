# Original-text recovery

Reword keeps the latest confirmed passage original and result in the running
application, independently of saved History. Copy original is explicit; Clear
removes this pair and the workspace Undo buffer. Ordinary word corrections,
rejected writes, cancelled jobs and unsafe fields do not populate it. Restarting
starts empty. This is bounded session memory, not encrypted or swap-proof memory.

Each side is limited to 50,000 Unicode characters and 100,000 UTF-16 units.
Invalid, unchanged or oversized pairs leave the previous pair intact. The same
hard operation ceiling now applies in Engine, Store and Settings; an earlier
100,000-character override bypass was reproduced and fixed.

## Delivery and lifecycle

Workspace records recovery only after applying a matching result. Native Service
emits passageApplied after confirmed replacement and before optional History
storage. The Fcitx addon sends a confirmed passage through a dedicated local
channel before optional History storage. No Clipboard write or disk mailbox is
used for this delivery.

The channel uses QLocalSocket/QLocalServer and an endpoint derived from the
absolute data-directory identity. UserAccessOption limits the server to its
account. Before even greeting a server, the client authenticates its OS account:
Linux SO_PEERCRED must match the effective UID; Windows named-pipe server PID and
TokenUser SID must match the current account. Query failures reject the peer.
A predictable endpoint alone is not authentication: independent review caught
that a different account could otherwise pre-bind the name. Same-account
malicious processes are outside this isolation boundary.

RWH1/RWR1 establishes the connection. RWP1 frames contain two checked little-endian
UTF-16 lengths and exact units, preserving even unpaired units. The maximum frame
is 400,012 bytes. Four clients maximum, one-second greeting/partial-frame/ACK
deadlines, one current and one newest queued client frame bound resources.
Incomplete, extra and malformed frames never partially update recovery. A STAT
diagnostic returns only a delivery generation count, never passage text.

The main process removes a stale endpoint only after taking its singleton lock.
Fcitx pumps Qt socket events from its existing callbacks/timer; it never waits
synchronously for a socket on the input path. Input processing requires a live
handshake, and confirmation rechecks app permission and lock/field safety.
Disconnect clears pending context, jobs and retry tracking. Quit/crash therefore
disables addon work; reconnect requires the app to return. Failed passage
delivery emits a fixed English error notification without text content.

Client destruction suppresses outward callbacks because owner fields captured
by those callbacks may already be destroyed. Guards also allow lifecycle and
ACK receivers to destroy the client synchronously without subsequent access.

## Evidence and remaining acceptance

Observed failures before fixes include unreachable original Copy after stale
workspace Undo with History off, the operation-limit bypass, obsolete queued
frames after a reentrant ACK, callbacks during destruction, and actual Wayland
correction continuing after clean GUI Quit. These regressions now pass.

Seven local CTest suites pass after transport/authentication changes (23.42s).
Tests cover wire bounds and exact UTF-16, partial/oversized frames, timeouts and
client limits, newest-only queueing, callback reentry/destruction, Controller
History-off delivery, restart, and explicit offscreen Copy. A Clipboard sentinel
asserts delivery does not overwrite unrelated content.

Private GNOME/Fcitx Qt and GTK runs pass real input, retry, password and app
Quit/crash/restart scenarios. Qt additionally verifies confirmed-passage delivery
with History off using only the app's text-free generation count, then native
Undo. GTK selection without a reliable anchor remains refused unchanged. Exact
original delivery and Copy are separately covered by offscreen Controller tests.
These fixtures do not prove all applications, physical lock behavior or ordinary
Windows 10/11 desktop acceptance. Final Qt and GTK reruns with peer authentication pass. OS peer checks have
independent source review. A separate Linux CI probe runs a synthetic server as
UID 65534 and the client as root, with the socket deliberately world-writable;
it must receive no greeting. The privilege is solely for disposable test-account
separation, never required by Reword. Local unprivileged CTest skips this probe.
Fresh Windows CI and the cross-account CI execution are pending at this edit.
