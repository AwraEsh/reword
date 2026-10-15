# Reword product contract

Approved by the user on 2026-10-15. This file is the durable requirements source.

## Product and quality
- Offline desktop writing assistant for Windows 10/11 x64 and Debian-based Linux, X11 and Wayland.
- C++20, Qt 6 Quick/QML, CMake, SQLite, Hunspell. No cloud processing or telemetry.
- Safety, preservation of text and privacy come first, followed by stability, accuracy, speed and visual craft.
- English-only UI, notifications and tray. Persian/English content, independent language switches.
- Restrained professional UI, proper keyboard access, scaling, dark/light mode, bidi content.
- No placeholder claims of complete system-wide support; publish per-adapter capabilities and verified application matrix.

## Live correction
- Correct high-confidence misspellings at word boundaries, preserving delimiter and case.
- Rank dictionary candidates with edit distance, keyboard proximity, frequency and bounded local context.
- Protect valid words, names, personal dictionary, identifiers, code, URLs, emails and paths.
- Same word at same location: correct first attempt, skip immediately retyped second attempt, correct third, alternating.
- Reset retry session on focus/caret navigation, continuing text, or 30 seconds.
- Revalidate field, selection/caret and expected text before any write. Refuse uncertain/stale operations.
- Undo must not overwrite subsequent user edits. Original remains copyable after a stale undo.
- Explicit personal dictionary and custom replacements; no permanent learning from temporary rejection.

## Selected text and rules
- Configurable shortcut: immediate replacement with guarded Undo.
- Independently switch spelling, abbreviations (im -> I am), capitalization, Persian character normalization, half-space and individual punctuation categories.
- Disabled categories must not make their own edits. No speculative semantic rewriting.
- Unsupported native selection has an honest internal editor fallback; never silently hijack the clipboard.
- At most 50,000 Unicode characters per operation; reject larger text intact.
- One operation at a time, cancellation and 10-second deadline. Never apply partial results.

## Privacy and application access
- New applications initially disabled; notify with Yes, Not now, No.
- Yes persists enabled. Not now stays disabled and asks on first use on next local calendar day.
- No persists disabled without reminders. Closing notification never grants access.
- Coalesce requests. Actionable native notifications or a small actionable app window fallback.
- Keep detected applications in Settings; identify by executable, never persist window titles.
- Application allowance cannot override password/unknown-field/locked-session protection.
- Browser master switch and per-application controls. Terminal, password manager and code editor defaults disabled.
- Do not log typed text; do not capture passwords; clear ephemeral buffers on unsafe focus.
- Educational correction notifications OFF by default. Optional word-pair counts OFF by default.

## Tray and storage
- Tray remains active after closing Settings; active/paused status, pause indefinitely or hours/minutes, resume, settings, quit.
- Timed pause survives sleep/restart until its deadline.
- Shortcut history saves original and corrected text with timestamp, independent Copy buttons, delete one and clear all.
- Never save everyday typing in history.
- Defaults: 100 records, 20 MiB total original+result UTF-8 payload, 30 days. Evict oldest to enforce every limit at startup and insert.
- History toggle is separate from deletion. User-only local data permissions; no claim of encryption at rest.
- Word-pair statistics, insights and a practice view; no sentences stored in statistics.

## Distribution and GitHub
- Simple single .exe installer on Windows, bundling application dependencies; no manual Qt/runtime setup.
- Linux AppImage preferred for simple launch; .deb also supplied. Explain unavoidable host integration for Wayland/tray.
- Disciplined Git, meaningful commits with tests/docs; private GitHub repository in authenticated user's account.
- Repository prepared for later public visibility: no credentials, private content, runtime database, host paths or downloaded dependencies.
- Do not make repository public without a separate user instruction.

## Evidence and handoff
- Test policies, retry cycle, language independence, ambiguous words, stale edits/Undo, history limits, pause and security boundaries.
- Real platform integration probes before claiming support, including Windows and GNOME Wayland.
- Aim for word correction p95 under 50 ms on test hardware; measure rather than assume.
- Multi-hour stability/resource acceptance required before declaring production ready.
- Persist implementation ledger, architecture, build/package instructions, compatibility evidence and remaining work.
