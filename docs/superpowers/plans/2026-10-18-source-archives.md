# Verified Debian source archives implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this inline under the approved distribution requirements.

**Goal:** Assemble the exact complete Debian source packages referenced by the runtime inventory, with verified file bytes and a release-ready index.

**Architecture:** A separate offline build tool reads runtime-manifest.json and locally collected .dsc files. It verifies exact Source/Version and every Checksums-Sha256 archive entry, copies into a temporary sibling directory, and publishes only a complete bundle. Source acquisition and remote release publication remain subsequent steps; the application and ordinary packager never download sources.

**Tech stack:** Python standard library; existing packaging unittest/CTest discovery.

**Spec:** docs/REQUIREMENTS.md, THIRD_PARTY.md, docs/LINUX_PACKAGING_CHECKS.md.

## Global constraints

- No user text, network access, dependency installation or archive extraction.
- Exact source epochs/versions are preserved; binary rebuild versions are not substituted.
- Missing files, modified bytes, unsafe paths, duplicate descriptor identities and mismatched size/checksum inventories fail before publishing output.
- Existing output is never replaced. Failed staging leaves no output or temporary directory.
- .dsc signatures are retained but not authenticated by this tool: acquisition must establish descriptor provenance independently.
- This deliverable does not claim full source delivery or accepted final installers.

## Review focus

- Signed cleartext .dsc files and unsigned descriptors yield the same control fields; malformed armor is refused.
- Checksums cover all files named by legacy Files/Checksums-Sha1 fields when present.
- Path traversal, symlinks and special files cannot read arbitrary files into an artifact.
- Large/truncated/growing inputs must not produce accepted partial output; expected bytes bound each copy.
- Multiple requested versions of one source retain separate directories and exact identities.

### Task 1: Offline verified assembly

**Files:** Create scripts/package_sources.py and tests/test_package_sources.py.

**Interfaces:** CLI `python3 scripts/package_sources.py --manifest runtime-manifest.json --input collected-sources --output new-source-bundle`; `assemble(manifest: Path, source_root: Path, output: Path) -> dict` returns the published index. The index records schemaVersion, runtimeManifestSha256, exact source package/version, relative descriptor/archive paths, sizes and SHA256 hashes.

- [x] Write whole-command temporary-file tests: exact epoch, multiple versions, signed descriptor, missing version/archive, changed archive/size, traversal/symlink/FIFO, duplicate fields/identities, SHA256 inventory mismatch, preservation of pre-existing output and atomic failure cleanup.
- [x] Run new suite and observe missing-tool failure before implementation.
- [x] Implement bounded strict Deb822/.dsc parsing and streaming checksum-verified copying; no archive extraction. Reject invalid metadata and unsafe inputs.
- [x] Run new suite, both existing packaging suites, py_compile and diff checks; commit verified assembler.

### Task 2: Actual source acceptance and release integration

**Files:** docs/SOURCE_DISTRIBUTION.md and docs/CURRENT_HANDOFF.md; later explicit acquisition command and release workflow.

- [x] Retrieve a real pinned Hunspell source package from Debian snapshot; retain descriptor provenance and verify the complete package through the assembler. Record exact hashes and scope.
- [x] Inventory all 78 exact source identities and total source archive sizes before collecting large archives; preserve disk headroom.
- [ ] Implement and verify explicit acquisition with authentic metadata/version checks, complete bundle publication under distributor control, and build/replacement instructions. No latest-version substitutions.
- [ ] Complete Windows source/notices materials and final installer pairing. Obtain independent whole-branch review and fresh Linux/Windows CI before integration.

## Integration ledger

PR #8 merged as a39913d from f365e5e after Linux/Windows push run 39038063500 and PR run 39038124521 succeeded. Its 47 packaging checks and real staged launch do not establish source delivery. This branch continues that remaining gate.

Review ledger: two Important races reproduced independently. New regressions
observe accepted substituted symlink bytes and parent symlinks before the fix;
new boundary tests cover no-replace publication and FIFO refusal. The fix uses
opened directory descriptors, O_NOFOLLOW/O_NONBLOCK/fstat and renameat2 with
RENAME_NOREPLACE. All 68 packaging checks pass. No deferred review findings.
