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
requests text. Adapters recheck the expected target and text before writing.
Desktop APIs cannot provide a universal cross-process atomic edit, so support
must be tested per application.

On X11, AT-SPI runs in `reword-native-helper`. Bounded JSON frames pass over its
parent-owned stdin/stdout pipes. This keeps its GLib event loop separate from the
Qt Quick GUI. Only plain single-line editable fields are currently supported.

Windows uses UI Automation, with bounded Win32 snapshots for standard Edit
controls. Hotkeys use a message-only native window. Elevated applications remain
subject to Windows access restrictions.

Wayland live correction belongs to the Fcitx5 addon, using its input context and
surrounding text. It is disabled on X11 to avoid two correction cycles. Pending
edits are observed before counts/history are saved. The addon shares the GUI's
local store and passage-operation lock. Host input-method setup is required.

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
