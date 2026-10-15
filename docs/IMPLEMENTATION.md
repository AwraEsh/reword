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
