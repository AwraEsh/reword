# Architecture

Reword separates correction, persistence, desktop access and presentation. There
is no remote correction service.

## Text processing

`src/core/core.*` owns language switches, protected spans, Hunspell candidates,
frequency ranking, writing rules and the same-location retry cycle. Dictionary
and frequency-data provenance is in `assets/dictionaries`. A frequency rank is a
heuristic, not a measured probability of correctness.

Live native correction considers newly completed words in a verified insertion
at the previous caret. It preserves delimiters and the unfinished next word.
More than 32 completed words in a single insertion is treated as a large paste;
use selected-text correction for that case. Failed native writes do not consume
an alternating correction attempt.

Passage processing has a character cap, cancellation and a deadline. Its output
is applied only as a complete result. Disabling a language disables its rules as
well as spelling corrections.

## Desktop boundaries

`src/platform/adapter.h` is the native inspection/replacement/shortcut boundary.
The service first requests metadata and applies the access policy before it
requests text. Adapters recheck the expected target and text, then ask the
service to confirm the app's current mode, pause state, configuration revision
and cancellation before submitting an edit. Selected-text edits also check the
operation deadline. An authorization check lasting 50 ms or longer is rejected.
The adapter returns the provider's acknowledgement and the state it observed
immediately after the write. The service uses the confirmed text, caret and
selection from that observation to record Undo and history. Undo still takes a
fresh snapshot and checks the current policy before submitting a write.
Desktop APIs cannot provide a universal cross-process atomic edit. An edit
already sent to a provider can still complete during a policy or target change.

On X11, AT-SPI runs in `reword-native-helper`. Bounded JSON frames pass over its
parent-owned stdin/stdout pipes. This keeps its GLib event loop separate from the
Qt Quick GUI. Only plain single-line editable fields are currently supported.
The helper asks for authorization after validating the field and again after
refreshing the lock and field state. Each policy reply has a short deadline.
Provider queries have their own time limits. Focus and target change events
observed during either check invalidate the edit. A broken handshake closes
the connection.

Windows uses UI Automation, with bounded Win32 snapshots for standard Edit
controls. It refreshes the expected field after the first authorization check
and verifies policy again before calling SetValue. Hotkeys use a message-only
native window. Elevated applications remain subject to Windows access restrictions.

Wayland live correction belongs to the Fcitx5 addon, using its input context and
surrounding text. It is disabled on X11 to avoid two correction cycles. Pending
edits are observed before counts/history are saved. The addon shares the GUI's
local store and passage-operation lock. Host input-method setup is required.
The core's `wordBeforeBoundary` is for an input method that sees the delimiter
before the application inserts it. It uses the same token and protected-span
rules as live observation after insertion. It takes a UTF-16 caret offset.

The X11 native helper can also report a metadata-only witness for a focused
multiline text widget. It verifies the active window and provider process
before and after refreshing the field's eligibility. The witness is separate
from the adapter's single-line read and write capabilities. A future IBus
client must bind that witness to the current input context and read the text
from IBus. Do not query an application's accessibility provider synchronously
from the IBus key callback. The application can be waiting on that callback.

The optional X11 IBus engine prepares spelling edits on a worker with one
replaceable request. An ordinary letter can predict the next surrounding
snapshot so the worker has time to prepare before the delimiter arrives.
An edit can use only a result whose complete text, caret, context generation,
configuration revision, and delimiter match the actual IBus observation.
Focus loss and blocked policy discard the result. A missing result leaves
the key with the application. Dictionary changes and correction settings
advance the revision in the same SQLite transaction as the change.

The development IBus engine records one live Undo after observing the exact
result of a correction. The configured Undo key can apply that record only
with a matching current observation, the same authorized target and the
same settings revision. Physical modifier presses keep the observation for
the following shortcut key. Commands that could edit the document consume
the observation. The engine waits for a new observation before it can make
another guarded edit.

With `REWORD_IBUS=ON`, the engine advertises a short-lived X11 Undo route
for the verified editor window. The property contains only the window ID
and a monotonic timestamp. While the route is fresh and matches the
foreground window, the native helper releases only its Undo grab.
Focus changes and expiry return the chord to the native helper. Fix and
Pause keep their existing grabs. The route changes key delivery only. The
engine still checks focus, permission and current text before applying Undo.

The opt-in Linux browser host verifies the configured extension origin and
gets the browser executable path from its parent process. Native messaging
frames have a 4 KiB limit. The host uses the same application mode, browser
toggle, Pause state and configuration revision as the desktop adapter.
It checks current policy before parsing a word request and again before
returning a suggestion. A persistent port keeps the dictionary loaded.
Pause and Resume advance the configuration revision in the same
transaction as the setting change.

The test extension checks browser policy before reading the field value.
It compares field identity, focus, input generation, text, caret and
settings revision after asynchronous work. Only then does it replace
the reported range. The current fixture does not cover web editors that
need a page-level input event or a browser-native Undo entry.

The host uses the shared retry cycle and returns an in-memory continuation
token with each candidate or deliberate skip. The extension sends it again
only after a verified delete and rewrite in the same location. A canceled
proposal cannot confirm its continuation token. The host keeps a digest
of the original word for that comparison.

## Threads and storage

The native service runs on a worker thread. Full-passage work uses asynchronous
workers; QML receives results and checks that its editor text has not changed.
The Fcitx addon also performs passage work outside its input event handler.

`src/core/store.*` uses separate SQLite connections per Store instance. It saves
settings, application decisions, personal words, replacements, optional word-pair
counts and selected-passage history. Everyday typing is not saved as history.
History is pruned at startup and insertion. Storage is local and is not encrypted.

The GUI and addon use a user-local lock for passage jobs. The GUI's second-launch
activation uses a user-only local socket with a bounded `show` message; it does
not listen on TCP. Text supplied by users is rendered as plain text, including
history and dictionary labels.

## Verification

Core tests exercise correction and persistence. Service tests exercise actual
service policy with a deterministic adapter. UI tests load the real QML and
check stale results, cancellation, storage failures and remote-resource requests.
Native probes use synthetic GTK/Win32 fixtures. These layers complement each
other; a passing synthetic probe does not establish compatibility with every app.
