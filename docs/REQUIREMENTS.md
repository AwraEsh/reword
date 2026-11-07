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
- Disciplined Git and meaningful commits with tests/docs in the authenticated user's GitHub account. The initial private-repository request was superseded by the explicit public-repository request.
- Repository prepared for later public visibility: no credentials, private content, runtime database, host paths or downloaded dependencies.
- User explicitly requested PUBLIC visibility after initial private creation. Public source must remain free of private/runtime data.

## Evidence and handoff
- Test policies, retry cycle, language independence, ambiguous words, stale edits/Undo, history limits, pause and security boundaries.
- Real platform integration probes before claiming support, including Windows and GNOME Wayland.
- Aim for word correction p95 under 50 ms on test hardware; measure rather than assume.
- Multi-hour stability/resource acceptance required before declaring production ready.
- Persist implementation ledger, architecture, build/package instructions, compatibility evidence and remaining work.

## 2026-10-18: release and local installation instruction

- After stability and quality are demonstrated, update GitHub-facing documentation
  for an ordinary user: simple installation, first use, limitations, pause/privacy,
  updating and removal. Keep developer build/audit material separate from that guide.
- Publish a real GitHub release with the verified Windows installer, Linux packages
  and matching checksums/source materials only after the quality/stability gates pass.
- Install the accepted Linux build on the user's current machine after those gates;
  do not substitute an earlier development package. This instruction authorizes that
  installation when the checks pass. Native host integration changes must be clear
  and documented; system/root actions may still need the host's authentication.
- Green CI alone is insufficient for release or local deployment. Preserve the full
  desktop compatibility, text-safety, rendered UI and active stability requirements.
## Non-disruptive consent notifications — 2026-11-06

The user confirmed that the newly installed build now detects applications and
shows consent. New consent must not raise/activate Reword or steal focus from
the current application. Use GNOME Shell native notifications on Linux and a
native notification near the Windows notification area, requesting a 7000 ms
display duration with OS-managed dismissal/fading. Do not claim exact fade
timing where the desktop controls it. An ignored notification leaves the app
unauthorized and keeps its pending request available inside Reword. Present
the in-app consent when the user intentionally returns to Reword, without
forcing an inactive window forward. Repeat ignored requests later with a
bounded cooldown/coalescing policy, not on every keystroke/window switch.
Explicit No remains permanent until the user changes it; Not now retains the
next-calendar-day rule. Native actions where supported must retain Yes,
Not now and No; fallback UI must not silently authorize an application.
The user explicitly makes these three buttons mandatory on GNOME Shell's
native consent notification; a text-only GNOME notification is insufficient.
Button clicks must apply their decision without activating Reword, and stale
buttons must never override a newer Settings decision.

## Per-application correction mode — 2026-11-06

The user explicitly requires an independently selectable mode for every
application in Settings, with English labels:

- **Automatic:** discover the active application and correct confident mistakes
  automatically at word boundaries, without requiring a click or shortcut for
  each correction. Preserve typed delimiters, independent language switches,
  sensitivity/ranking safeguards and the existing same-location first/second/
  third alternation (correct, preserve the rewritten mistake, correct again).
- **Shortcut only:** disable live automatic spelling and normalization for that
  application. Permit explicitly requested supported selected-text correction
  via the configured shortcut, with the normal safety, cancellation, history
  and Undo guards. Merely observing typing must not change the text.
- **Disabled:** do not process or change that application's text, including
  correction shortcuts. Application discovery/Settings visibility can remain
  metadata-only; no text, statistics or history is collected from blocked fields.

Modes must persist by executable identity and apply immediately to an active
application and outstanding operations. Mode changes must participate in policy
revision/revalidation; switching to Disabled cancels pending native work. A new
application remains unauthorized until explicit consent. Yes may select the
default authorized Automatic mode; this must be clear in the consent wording.
No keeps it disabled without reminders; Not now remains unauthorized until a
later explicit choice. Migrate existing allow/deny/later/unknown decisions
without silently granting new access. Global pause, browser exclusion, language
rules and password/unknown-field protections retain precedence over app mode.
No mode authorizes unsafe replacement or makes an unsupported field editable.

Acceptance requires real typing in Automatic, no edits while typing in
Shortcut only, successful explicit shortcut correction where supported, and no
text processing in Disabled, on both Linux and Windows. Include mode changes
during active correction, restart persistence, consent migration and the retry
alternation in automated/native checks.

**Remove rule** revokes and deletes the saved decision for that executable and
clears any pending permission prompt for it. The app can appear again when
detected, without authorization. Removing a rule has no effect on history,
personal words or other apps.

The sidebar credit must read **Built by AwraEsh for AwraEsh**.

## Upgrade and replacement requirement — 2026-10-29

On 2026-11-06 the user permits resetting existing preferences if necessary to
fix operation; preserve this as permission, not a requirement to discard data.
The same request reiterates working Windows and Linux behavior as acceptance,
not merely build compatibility.

The user requests replacing the previous local installation with each verified
new build, rather than leaving competing versions or launchers installed.
Updates on Windows and Linux must manage obsolete application files safely,
keep a stable application identity/install location, avoid duplicate instances
and preserve settings, consent decisions, personal dictionaries and history.
Application binaries/resources and account data must have separate lifecycles.
Do not delete user data as part of upgrading or uninstalling an old binary.
Verify upgrade from a previously installed version on both platforms; a clean
installation test alone does not prove upgrade safety. Failed replacement must
leave a usable previous version or a clear recovery path. Linux AppImage,
desktop entry and icon replacement, Debian upgrades and the Windows installer
must each have documented ownership and obsolete-file cleanup rules. Remove
only files owned by the previous Reword installation, never arbitrary files in
the install directory. Local replacement is authorized once the new build is
verified; do not present an unverified build as stable.

After updating GitHub, publish the verified release with a Windows x64 setup
`.exe`, Linux x86-64 AppImage and Debian package, plus checksums and installation
instructions. Record platform-specific test results and distribution materials
for the exact binaries before publishing.
