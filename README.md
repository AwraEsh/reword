# Reword

Reword is an offline spelling assistant for English and Persian. It corrects
mistakes while you type, with separate controls for each language and permission
settings for each app.

Delete a correction and repeat the same mistake, and Reword leaves it alone.
Repeat it once more and correction resumes. For longer passages, you can correct
selected text, undo the change, or copy the original from local history.

## Development status

Reword is available as a development preview. See
[Try Reword Preview](docs/TRY_REWORD.md) for the Windows download, local Linux
installation and current limits. The final stable release is still being tested.

The target platforms are Windows 10/11 and Debian-based Linux. X11 uses AT-SPI;
Wayland integration requires Fcitx5. Coverage varies by application, and fields
that cannot be checked or edited safely are skipped. See the
[compatibility notes](docs/COMPATIBILITY.md) for tested cases and limitations.

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
them into `.deps`. It does not install system packages. This setup is for
development; it is not an installer.

## Your data

Text stays on your computer. There is no cloud service or telemetry.
Selected-text history is stored locally and can be disabled or deleted. It is not
encrypted at rest. Everyday typing is not saved in history.

## Documentation

- [Requirements](docs/REQUIREMENTS.md)
- [Implementation progress](docs/IMPLEMENTATION.md)
- [Third-party licenses](THIRD_PARTY.md)

The source currently has no redistribution license. See [LICENSE](LICENSE).
