# Third-party components

- Qt 6: LGPL-3.0 / GPL / commercial; dynamically linked. https://www.qt.io/licensing/
- Hunspell: LGPL / GPL / MPL tri-license. https://github.com/hunspell/hunspell
- SQLite (through Qt SQL): public domain. https://sqlite.org/copyright.html
- AT-SPI2: LGPL-2.1-or-later. https://gitlab.gnome.org/GNOME/at-spi2-core
- Fcitx5: LGPL-2.1-or-later / component-specific. https://github.com/fcitx/fcitx5

Dictionary source and original license files must accompany every distribution.
The checked-in English and Persian dictionaries carry their original notices;
packaged builds copy these notices alongside the resources. No downloaded dependency belongs
in the Git repository.

## Word frequency ranks

The `*-frequency.json` assets contain derived wordfreq data by **Robyn Speer**
and the contributors credited in the attribution embedded inside each JSON file.
The data is licensed under **CC BY-SA 4.0**. Filtering and lowercase rank extraction
are documented in those files; the app does not implement the wordfreq tokenizer.

Original notices: [wordfreq-NOTICE.md](assets/dictionaries/wordfreq-NOTICE.md).
Pinned sources, hashes and counts: [frequency-sources.json](assets/dictionaries/frequency-sources.json).
The generator uses msgpack (MIT) only as a developer tool; Python and msgpack are
not needed to run Reword.


## Linux runtime inventory

The Linux packager records the original package and source-package version for
each copied Qt, QML, plugin and transitive library file. It checks the input
against Debian package checksums, then checks the staged file with SHA256.
Package-specific copyright files and the referenced common license texts are
included under `usr/share/reword/licenses/` in AppDir (under `/opt/reword/` in
.deb). `usr/share/reword/runtime-manifest.json` lists relative paths, component
identities and fingerprints; it contains no build-machine paths.

This is a notice inventory, not the complete source-delivery material. Matching
library source archives, build/replacement instructions and Windows-specific
runtime notices remain required before final binary-release acceptance. The
project's own license does not replace or restrict third-party license terms.
