# Source materials for bundled Linux libraries

The runtime inventory identifies exact Debian binary and source package versions.
That inventory and the copied notices do not replace the corresponding source
packages. Release preparation must preserve the complete matching source package,
including Debian patches and build metadata, under our distribution control.

## Offline assembly

Collect the exact `.dsc` descriptor and every archive it names into an input
directory. Several packages/versions may occupy separate subdirectories. Run on
Linux with Python 3 and a filesystem/kernel supporting `renameat2`:

```sh
python3 scripts/package_sources.py \
  --manifest dist/Reword.AppDir/usr/share/reword/runtime-manifest.json \
  --input /absolute/path/to/collected-sources \
  --output /absolute/path/to/new-source-bundle
```

Use real paths: symlinks in input file ancestors are refused. The output parent
must exist and the output itself must not exist. The assembler checks exact
source identities, including epochs, and deduplicates binary packages from the
same source version. It requires SHA256 entries for every archive; any `Files`
or `Checksums-Sha1` inventory must name the same files with matching sizes.
Every declared digest is checked during copying. Archives are never extracted.
A complete bundle is published with atomic no-replace rename; errors remove
only this invocation's temporary staging directory. Existing output is preserved.

`source-index.json` records the runtime manifest SHA256, source identities,
relative paths, exact byte counts and SHA256 of descriptors/archives. It contains
no build-host paths. Check the paired runtime manifest before attaching a source
bundle to an installer release.

Limits: descriptor 2 MiB, runtime manifest 16 MiB, 2,048 descriptors, 4,096 runtime
components, 256 archives per descriptor, 16 GiB per archive, 64 GiB total. Assembly
requires the expected output bytes plus 64 MiB free before copying. Reads also
reject growing/truncated files. This is a build tool; it adds no installed app
runtime dependency or network activity.

## Trust boundary

The assembler preserves a cleartext OpenPGP descriptor exactly but does **not**
authenticate its signature or establish Debian archive provenance. Its index
states `descriptorAuthentication: not-verified`. Successful assembly proves
identity/integrity relative to the supplied descriptor, not that an untrusted
descriptor was issued by Debian. Acquisition must separately establish that trust
through authenticated archive metadata or verified signing keys, preserve that
evidence and then pair the exact files with the release. Never substitute the
latest source version for a missing requested version.

Debian snapshot file hashes may have multiple historical filenames. Choose the
archive name from the exact descriptor's checksum fields and match bytes/hashes;
do not assume the first filename returned for a snapshot hash is the right name.

## Recorded development acceptance, 2026-10-18

Twenty-one temporary-file tests cover whole-command assembly, source epochs,
multiple versions, cleartext signatures, absent/wrong versions, changed bytes and
sizes, traversal, symlinks, FIFO refusal, conflicting metadata, ambiguous inputs,
output preservation and cleanup. A source review reproduced two races: check/open
could follow a substituted symlink, and ordinary rename could replace a new empty
output directory. Regression tests fail before the fixes; actual file opens now
use no-follow directory descriptors and nonblocking file opens with fstat, and
publication uses `RENAME_NOREPLACE`. All 68 packaging tests pass in 9.84 seconds.
The primary checkout also passes all seven CTest suites in 44.69 seconds after
PR #8 integration. No C++ or QML behavior changes in this branch.

A real Hunspell `1.7.2+really1.7.2-10` source package was obtained from Debian
snapshot over HTTPS and accepted through this tool using the Hunspell component
of the actual runtime inventory. The retained descriptor is 2,433 bytes:
`790e83b70e06f1daf5e7d9a6635987a7c771b09f7e7a79bb170056a896f6b614` (SHA256).
Both archives pass the descriptor's SHA256, SHA1 and MD5:

| Archive | Bytes | SHA256 |
| --- | ---: | --- |
| hunspell_1.7.2+really1.7.2.orig.tar.gz | 1,536,202 | 11ddfa39afe28c28539fe65fc4f1592d410c1e9b6dd7d8a91ca25d85e9ec65b8 |
| hunspell_1.7.2+really1.7.2-10.debian.tar.xz | 24,940 | d1d4761a9d5a39a5cf8de65103cd94265578d27b72410b75591767d73c384fe0 |

Ignored build evidence lives in `.deps/source-acquisition/hunspell/` and
`.deps/source-acquisition/hunspell-verified/`; the test logs are
`.deps/sources-red.log`, `.deps/sources-review-red.log`,
`.deps/sources-review-green.log`, `.deps/sources-final-tests.log` and
`.deps/sources-primary-ctest.log`. The subset manifest fingerprint is
`34a189ccd4351520afaa2416a00b06f28aa7768e4807449c796ba455d6439c58`.
This is one-package integrity acceptance, not delivery of all 78 source identities,
OpenPGP authentication, remote publication or a final installer/source pairing.

## Source archive inventory

All 78 exact source identities from the real Debian 13 runtime inventory were
located in Debian snapshot's HTTPS metadata. Their descriptor/archive sizes total
440,968,366 bytes (about 420.5 MiB), before filesystem overhead and build
instructions. The largest are gcc-14 (96,923,996 bytes), OpenSSL (53,304,817),
qt6-base (46,573,559) and qt6-declarative (36,646,153). Initial DNS failures were
retained and retried; the completed metadata inventory has no failed identity.
Historical filename aliases for identical content are recorded rather than
substituting another package version.

This is availability/size evidence, not authenticated source acceptance: only
Hunspell's actual archives have been collected and assembled so far. The ignored
`.deps/source-acquisition/inventory.json` retains exact versions, snapshot content
hashes, locations and sizes; `.deps/source-inventory.log` and
`.deps/source-inventory-retry.log` retain both attempts. Full collection and an
assembled copy require roughly twice the archive size locally; check free space
again before starting, since other processes can consume it.

## Open release gates

- Preserve exact-version source identities when collecting all 78 packages. Check
  available disk space before large downloads; do not remove unrelated files.
- Implement explicit acquisition with authenticated metadata and resumable,
  checksum-verified downloads. Package assembly remains offline.
- Collect and publish every required source package under distributor control,
  with preserved license texts and build/replacement instructions.
- Complete Windows Qt/Hunspell/VC runtime source and notice materials.
- Verify the final exe/AppImage/deb and source bundle pairing, then perform the
  remaining clean-install, desktop and active stability acceptance in
  [CURRENT_HANDOFF.md](CURRENT_HANDOFF.md).

References: [Debian source package structure](https://www.debian.org/doc/manuals/debian-handbook/sect.source-package-structure.en.html),
[Debian snapshot](https://snapshot.debian.org/),
[Qt open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations).
