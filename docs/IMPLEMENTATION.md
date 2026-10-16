# Reword implementation ledger

Authority: the approved implementation plan in this conversation (2026-10-15).

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
