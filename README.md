# Reword

Reword fixes spelling mistakes as you type. It runs locally, supports English and
Persian, and lets you decide which apps it can work in.

If you delete a correction and type the same mistake again, Reword leaves it alone.
Try once more and it corrects it again. You can also fix a selected passage and
recover the original from a small, local history.

## Status

Reword is being built. The core tests run on Linux; desktop integration and
installers are still in progress. It is not ready for everyday use yet.

The targets are Windows 10/11 and Debian-based Linux, including X11 and Wayland.
Support depends on what each app exposes to the desktop's text-input and
accessibility APIs. Reword leaves fields alone when it cannot safely identify or
edit them.

## Build from source

You need a C++20 compiler, CMake 3.22+, Qt 6.4+ and Hunspell. Linux integration also
uses AT-SPI2, X11 and Fcitx5.

For a local Debian development setup without root:

```sh
python3 scripts/bootstrap-local.py
scripts/env.sh cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
scripts/env.sh cmake --build build -j2
scripts/env.sh ctest --test-dir build --output-on-failure
```

The bootstrap downloads packages from your configured apt repositories and unpacks
them into `.deps`. It does not install system packages. Packaged releases will not
need this setup.

## Your data

Text stays on your computer. There is no cloud service or telemetry.
Selected-text history is stored locally and can be disabled or deleted. It is not
encrypted at rest. Everyday typing is not saved in history.

## Project notes

- [Requirements](docs/REQUIREMENTS.md)
- [Implementation progress](docs/IMPLEMENTATION.md)
- [Third-party licenses](THIRD_PARTY.md)

The source currently has no redistribution license. See [LICENSE](LICENSE).
