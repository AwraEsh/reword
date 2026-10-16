# Current handoff — 2026-10-16

The approved scope is in [REQUIREMENTS.md](REQUIREMENTS.md). The repository is
intentionally public at the user's later request. Do not change visibility back
to private. Keep runtime data, local captures, dependencies and build outputs out
of Git. No release has been accepted as production ready.

## Source and verification

- C++20/Qt Quick application, shared correction/store core, X11 helper, Windows
  adapter and Fcitx addon are implemented. See [architecture](ARCHITECTURE.md).
- Latest committed service change before this handoff: c02bf5d. It fixes completed
  words being missed during typing bursts and retry advancement after rejected
  writes. Four local CTest suites pass.
- Current Wayland hardening adds canonical hotkeys, full-context code protection
  and confirmation before successful retry advancement. Repeatable GNOME/Fcitx
  probes pass on real Qt and supported GTK3 fields. See
  [compatibility evidence](COMPATIBILITY.md) for the exact limits.
- Windows CI at c02bf5d passed native field/password/hotkey probes and built an
  installer. The new clean-runtime install/uninstall check still needs CI proof.
- Linux native CI has been failing fixture focus. AT-SPI runtime activation was
  added; the next harness version also supplies an Openbox window manager.
- Frequency tables contain 93,959 English and 31,166 Persian ranks with pinned
  hashes and attribution. They are heuristic ranking data, not universal coverage.
- The detached two-hour idle run tests an earlier binary, identified by SHA256
  ca6939b84c15fdc79ae32af5b5cafaecd8e68d6e1f07f03931e230eea8e2729a.
  It completed 7,202 seconds with 1,430 samples: app maximum RSS 96,960 KiB,
  helper maximum RSS 25,072 KiB and app CPU time 118.9 seconds. Its report says
  passed and both process handles are now terminal. RSS alone does not establish
  total allocated memory or exclude swapping. This idle baseline does not replace
  an active workload test on the final packaged binary.

## Commands

```sh
scripts/env.sh cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
scripts/env.sh cmake --build build -j2
scripts/env.sh ctest --test-dir build --output-on-failure
/usr/bin/python3 scripts/test-native-wayland.py --fixture qt
/usr/bin/python3 scripts/test-native-wayland.py --fixture gtk
python3 scripts/test-native-linux.py
```

Build serially. Do not overlap builds in the same directory. Local rootless tools
and dependencies live in ignored `.deps`; CI uses its own packages. Native test
scripts name their required host tools in their help/docstrings.

## Required work still open

1. Verify fresh CI for the updated X11 desktop harness and Windows installer test;
   fix failures rather than weakening assertions.
2. Audit remaining retry reset cases on Wayland (cursor movement, continuation,
   timing), selected-operation cancellation/staleness, settings/permission changes
   during operations and actual lock/unlock behavior.
3. Verify Windows user-only NTFS storage permissions; Qt file permissions alone
   are not sufficient evidence of a protected Windows DACL.
4. Complete rendered UI review at minimum size and scaled display, keyboard-only
   navigation, populated lists and dialogs, plus native Windows UI review.
5. Rebuild Linux AppImage/.deb from the final source; current local packages are
   older. Verify runtime closure, exact minimum glibc, host integration dependency
   declarations, bundled third-party notices and clean-environment launch.
6. Expand real compatibility tests to browsers and representative GTK/Qt/Windows
   editors, including focus races and fast typing. Supported synthetic fields do
   not prove broad application coverage.
7. Run a final multi-hour active/resource test and refresh latency measurements.
8. Produce matching versioned artifacts, checksums, repeatable install instructions
   and a requirement-by-requirement acceptance audit before declaring completion.

The source has no redistribution license yet; [LICENSE](../LICENSE) states the
current terms. Third-party licenses are recorded separately. Public visibility
does not grant a source license or prove installer acceptance.
