# Reword implementation ledger

Authority: the approved implementation plan in this conversation (2026-10-15).
This ledger is chronological; see [current handoff](CURRENT_HANDOFF.md) for the
latest status and remaining acceptance work.

- [ ] Native adapter feasibility and build toolchain.
- [ ] Safety policy, retry cycle, bounded offline correction, persistent data.
- [ ] Linux/Windows/Wayland adapters and guarded transactions.
- [ ] English Qt Quick UI, tray, settings, editor, history and insights.
- [ ] Tests, real integration probes, packaging and handoff.

Ruling: this is an empty, non-Git workspace; initialize develop in place, without a redundant worktree.
Ruling: no administrator installation; unpack Debian development packages in ignored .deps.
Ruling: no Windows host is available; Windows source/build recipe can be prepared, but runtime acceptance must remain pending.
Ruling: the current session is X11; Wayland runtime acceptance requires a real Wayland login and must remain pending until performed.

## 2026-10-16: integration hardening

- The source repository is PUBLIC at the user's subsequent explicit request; this supersedes the original private-only requirement.
- Core correction, SQLite storage, seven-page English UI, tray, consent and packaging code are implemented.
- Independent review found and prompted fixes for Windows target reacquisition, hotkey delivery, capped UTF-16 reads, protected-span rules, and addon dictionary installation.
- Linux AT-SPI now runs in a child helper process with bounded stdin/stdout JSON frames. It does not share the GUI's GLib loop. Pipe access is private to the parent/child; no network listener is created.
- All native writes revalidate the expected field/text. Unsupported fields remain untouched.
- Full-passage work runs asynchronously with one in-flight operation, cancellation and deadline. Fcitx selected-text correction also runs off its input loop.
- Fcitx commits are observed before history/counts are recorded; guarded Undo is implemented. Real Wayland acceptance remains pending.
- Local build passed. Core/UI suite passed five consecutive repeats after helper isolation.
- Real GTK single-line entry probe passed: metadata does not contain text; stale writes rejected; replacement confirmed; password field read blocked.
- GitHub CI at commit 07f575b failed: Windows COM header include order, Linux missing accessibility bus. Fixes are now implemented and need a fresh remote run.

Remaining acceptance: Windows build/installer CI, real Windows field/hotkey tests, real GNOME Wayland/Fcitx integration, packaged launch checks, extended stability and performance evidence. This is a development build, not production acceptance.

- End-to-end GTK service probe now verifies first/second/third correction, both Undo paths, pause, selected-text history and password exclusion. The probe waits for service initialization before creating its fixture.
- New regression tests cover Unicode half-space normalization, immediate cancellation, storage failure containment and mutual exclusion of full-passage work.
- Full-passage jobs share a user-local operation lock across the GUI service and Fcitx addon.
- Initial performance sample: 1,400 word calls, p95 17.15 ms; maximum 28.83 ms; 50,000-character synthetic passage 103 ms. This is a bounded synthetic sample, not a complete workload benchmark.
- A two-hour offscreen idle stability run started; its report and process handle remain local under .deps/soak. Do not claim it passed before the process finishes.
- Remote Linux CI passed at 86f2e9d. Windows progressed to a SetValue(BSTR) type error, now fixed for the next run.

- Windows and Linux CI both succeeded at eade977, including the Windows .exe installer artifact. Physical Windows application acceptance was not implied.
- Added independent punctuation categories and opt-in terminal-period / introductory-comma rules; all obey language and category switches.
- Added user-only local activation: opening Reword again raises the existing process instead of creating a second background instance. Activation tests passed.
- The first idle stability process ended before its requested two hours; marked interrupted, not passed. A detached replacement is running under .deps/soak-current with a binary SHA-256 in its report.
- Shared-desktop GTK tests can lose focus to other real applications. A failing run reported fixtureStillFocused=0. Controlled display tests are being prepared; earlier successful native probes remain valid for their tested conditions.


## 2026-10-16: offline ranking and UI privacy

- Added pinned, attributed wordfreq-derived frequency tables: 93,959 English and
  31,166 Persian entries. The tables rank Hunspell candidates; they do not promise
  coverage of every word. Source hashes and CC BY-SA attribution travel with the
  data. Generation uses developer tools only; application correction stays local.
- Added regression checks for broader spelling candidates, lowercase proper names,
  pause cancelling pending work, and saved markup never starting HTTP/HTTPS loads.
  User-provided labels now explicitly render plain text.
- Formatted C++ and QML sources for maintenance. Include order is preserved for the
  Windows COM headers.
- Synthetic frequency-engine sample during a parallel build: 1,400 calls, word
  p95 37.98 ms, maximum 71.83 ms; a 50,000-character passage completed in 273 ms.
  This is a workload sample, not a hardware-independent latency guarantee.
- A private headless GNOME Wayland session launched the actual Qt application with
  exit code 0. Fcitx correction in that session is still being tested.
- Windows CI at 5a4f4c2 passed, including the real Edit-control/password/hotkey
  probe and installer creation. Linux CI failed to observe the controlled GTK
  fixture focus; its accessibility activation environment is being corrected.
- The detached two-hour idle run remains live at the last check (runner 9896,
  application 9904). It tests an earlier binary whose hash is in its local report,
  and is not acceptance of later source changes.


## 2026-10-16: typing bursts and retry transactions

- Reproduced three failing service cases before the fix: a completed typo followed
  by the next partial word, two completed words in one poll, and alternation
  advancing after a rejected write. The same tests pass after the change.
- New service tests also cover offset changes, punctuation preservation, the
  first/second/third cycle, focus reset, stale Undo, protected context and refusing
  even to request password text. The native probes remain separate checks.
- Fresh full local build and all four CTest suites passed (14.70 seconds).
- CI's accessibility failure was traced to missing `at-spi2-core`, which owns the
  D-Bus activation service. Added that runtime package to the Linux CI setup;
  a fresh remote run is still required.
- A private GNOME Wayland session with actual keyboard injection and the explicit
  GTK Fcitx5 module corrected `teh ` to `the `. The module loaded, discovered the
  synthetic application and wrote its heartbeat. Broader Wayland acceptance is
  still pending; the first successful word is not a full acceptance result.


## 2026-10-16: real Wayland probes and independent review

- Added a repeatable private GNOME Wayland/Fcitx test using real virtual keyboard
  input, owned synthetic GTK/Qt fixtures and private storage. The remote-input
  D-Bus owner PID must match the compositor launched by the test.
- The real probe exposed canonical-key mismatch for Ctrl+Alt letter shortcuts;
  normalization on both sides fixed live Undo and Pause.
- Independent review found code-span protection and unconfirmed-write retry
  gaps in the Fcitx live path. Both were reproduced in real Qt input tests before
  fixing. Fcitx now shares full-context completed-word protection and commits a
  successful retry decision only after observing the expected edit.
- Qt probe passed rejected-write retry, first/second/third, live Undo, code-span
  protection, pause/resume, selected correction/history/Undo and password exclusion.
  The rejection fixture confirms that an actual IME event was refused.
- GTK3 probe passed its supported live cases. Selection remains unavailable in
  this input module, and no history is recorded for the unsupported operation.
- Local CTest suites passed again after the changes (18.27 seconds). Optional
  synthetic UI captures exercise all seven pages in light/dark modes.
- Linux CI now starts a private Openbox window manager because the adapter's
  focus scan requires active-window state, which bare Xvfb does not provide.
  Remote evidence for that updated harness is pending.
- Windows installer acceptance now installs per user into a path containing
  spaces, removes developer paths, loads the native Windows Qt platform, and
  tests uninstall preserving a user-added file. It is CI-only; execution pending.


- The detached baseline idle run completed 7,202.09 seconds and 1,430 samples;
  report state is passed and both runner/application processes exited. Maximum
  app RSS 96,960 KiB, helper RSS 25,072 KiB, app CPU 118.9 seconds. This describes
  the recorded older binary and resident memory only, not a final active-workload
  or total-memory acceptance result.
- Reviewed rendered synthetic captures of all seven pages in light/dark modes at
  1120x780. No overlapping content was observed in those captures. Switch contrast,
  smaller/scaled windows, populated lists, dialogs and Windows rendering still
  need closer review.
