# Authenticated source material implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline under the approved distribution requirements.

**Goal:** Authenticate every exact source descriptor before accepting its archives and collect the complete material without duplicate disk usage.

**Architecture:** An offline authentication module checks an explicitly trusted archive keyring -> signed InRelease -> Sources.xz SHA256 -> exact descriptor SHA256, with direct maintainer signatures for exact historical versions absent from supplied indexes. The existing assembler optionally requires this authentication. A subsequent explicit acquisition command can stream checked downloads into one new staging tree; ordinary packaging and application runtime stay offline.

**Tech stack:** Python standard library, gpgv, existing no-follow file helpers and packaging unittest/CTest suite.

**Spec:** docs/REQUIREMENTS.md, docs/SOURCE_DISTRIBUTION.md, previous source-archives plan Task 2.

## Global constraints

- Trust roots are explicitly supplied local keyrings, never arbitrary downloaded signer keys.
- Exact package/version and descriptor bytes must match; conflicting authenticated indexes fail closed.
- No latest-version substitution, archive extraction, host configuration changes or user data.
- GPGV runs in a private temporary home, with copied checked keyring bytes and bounded inputs/time. No ambient keyring or keyserver.
- Historical release dates are recorded; this authenticates fixed source artifacts, not repository freshness or an upgrade policy.
- Preserve existing outputs; only publish complete material. Check disk headroom before large downloads and never delete unrelated files.

## Review focus

- Unknown signer, changed descriptor, changed release and changed compressed/uncompressed index must fail.
- A descriptor disagreement with an indexed identity cannot fall back to another valid signature.
- Oversized compressed data/expanded index, unsafe relative paths and malformed duplicate metadata cannot escape resource limits.
- Source identities absent from current indexes may use an explicit maintainer root, but unknown signatures fail.
- Public evidence contains hashes and fingerprints, not build-host paths, incidental signer display text or user data.

### Task 1: Offline authentication and assembly integration

**Files:** Create scripts/package_source_auth.py and tests/test_package_source_auth.py; modify scripts/package_sources.py and source distribution docs.

**Interfaces:** `authenticate(config: Path, descriptors: dict[tuple[str,str], tuple[str,bytes]]) -> dict` returns schemaVersion/keyrings/indexes/sources evidence with exact identity, descriptor SHA256 and method. Config schemaVersion1 has maintainerKeyrings/archiveKeyrings path lists and indexes with release/sources/path (archive-relative signed Sources.xz path). Relative local paths are relative to the config directory. `assemble(..., trust_config: Path | None = None)` and CLI `--trust-config` require all descriptors authenticated when supplied; unconfigured integrity-only behavior remains explicitly not-verified.

- [x] Write tests using real ephemeral GPG keys/signatures and xz indexes. Assert direct signatures and archive chain evidence; reject unknown keys, modified bytes, wrong versions, index conflicts, duplicate fields, unsafe paths and oversized streams. Observe missing-feature failure.
- [x] Implement checked private keyring copies, gpgv bounded verification, streaming signed index checks and exact descriptor resolution. Integrate optional required authentication into assembly before publication.
- [x] Run all packaging checks and project CTest; preserve RED/GREEN evidence. Commit.
- [ ] Run actual authentication for all78 exact runtime source identities using retained Debian keyring and signed trixie/trixie-security indexes. Record hashes/method counts/trust boundary. Obtain independent review, address concrete findings with regressions, and run fresh Linux/Windows CI.

### Task 2: Complete acquisition/publication

**Files:** Explicit build-only acquisition command, tests and distribution documentation (to be specified against Task1 interfaces before code).

- [ ] Download exact descriptor-referenced archives directly into a bounded new staging tree, verifying expected sizes/digests. Authenticate descriptors before archives. Avoid a second full440MiB copy; maintain64MiB disk headroom.
- [ ] Verify complete material offline and publish under distributor control with build/replacement instructions, final installer pairing and checksums.
- [ ] Complete Windows source/notices and remaining desktop/stability gates. This plan does not mark the whole product complete.

## Evidence at start

PR9 merged b985207 from430000b after Linux/Windows runs39124936466/39124972344 passed. Integrated packaging CTest13.27s;68 packaging regressions. Exact source metadata totals440,968,366bytes. Direct signatures verify74/78 descriptors. Independently verified signed trixie/trixie-security Sources match74/78; the union covers78/78. Historical at-spi2-core/curl/glib2.0/sqlite3 versions use maintainer signatures; the four absent signer certificates are covered by archive metadata. Actual free space715MiB before indexes; no large archives downloaded yet beyond Hunspell.

Reference: https://manpages.debian.org/trixie/apt/apt-secure.8.en.html (archive authentication chain).

## Execution evidence

The initial19-test authentication feature run includes fourteen new behaviors
that failed on the missing module/CLI option; malformed signed xz subsequently
exposed an uncaught LZMAError and has its own RED/GREEN log. All87 packaging
checks pass18.39s. Actual authentication succeeds78/78:75 archive-index and3
maintainer-signature proofs. Source key expiry is refused even when gpgv emits
VALIDSIG and exits0; a real expired ephemeral key fixture locks this policy.
The at-spi2-core exact descriptor is authenticated by signed historical
trixie-proposed-updates metadata from20251208. A curl certificate renewal from
the official keyring service is accepted only with the unchanged trusted primary
fingerprint BFAE9E331A867A7C80D8EB78F4E4ACDBB8D08BE0.

The first full CTest attempt in this older distribution worktree cannot run the
recovery test because its binary is missing; the other suites finish. Rebuilding
the entire branch before rerunning, rather than treating that run as green.
Current free disk space is13GiB (external state changed; no cleanup performed).

The full rebuilt branch passes all7 CTest suites39.26s. Final read-only review
finds no Important/Critical issue and independently passes19 authentication tests.
Actual strict authentication passes78/78 with75 archive-index/3 maintainer proofs;
old direct at-spi2-core expiry is avoided through an authenticated historical index,
not a relaxed signature status rule. Source certificate renewal for curl retains
its already trusted primary fingerprint. Exact source archive acquisition started
in the task-owned ignored cache, not as an implicit packaging/runtime action.
