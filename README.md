# Reword

A private, offline writing assistant for English and Persian, with an English desktop interface.

**In development.** Native correction is capability-gated: unsupported or uncertain fields are left untouched. Windows and Wayland runtime acceptance is tracked separately from Linux builds.

## Build

C++20, CMake 3.22+, Qt 6.4+ (Quick, QuickControls2, Widgets, SQL, Concurrent, Test), Hunspell. Linux native integration additionally uses AT-SPI2, X11 and optional Fcitx5 development packages.

On Debian, a rootless developer setup is available:

```sh
python3 scripts/bootstrap-local.py
scripts/env.sh cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
scripts/env.sh cmake --build build -j2
scripts/env.sh ctest --test-dir build --output-on-failure
scripts/env.sh ./build/reword
```

The bootstrap uses your configured apt package indexes; refresh stale system indexes separately if a package URL returns 404. Runtime installation will not require this developer setup.

- [Product requirements](docs/REQUIREMENTS.md)
- [Implementation ledger](docs/IMPLEMENTATION.md)

No cloud API, telemetry or keystroke log. Selected-text history is local and is **not encrypted at rest**. Its retention is configurable and independent from deletion.
