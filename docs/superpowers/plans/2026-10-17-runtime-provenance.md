# Runtime provenance and notices implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Execute in this session under the already approved product plan.

**Goal:** Every copied Linux runtime file has a verified package identity, a matching copyright notice and a reproducible source-package identity.

**Architecture:** Record original paths while staging. Resolve system files through Debian's package database and rootless files through the downloaded .deb control metadata, verifying installed bytes against package md5sums. Publish SHA256 fingerprints and relative bundle paths; copy exact notices and common license texts. Corresponding source archives and Windows provenance remain required subsequent distribution gates, not implied by this inventory.

**Tech stack:** Python standard library, dpkg-query/dpkg-deb, existing CMake/CTest packaging suite.

**Spec:** docs/REQUIREMENTS.md (Distribution and GitHub), docs/LINUX_PACKAGING_CHECKS.md, docs/CURRENT_HANDOFF.md.

## Global constraints

- No runtime user data or build-machine absolute paths in published manifests.
- No network requests or implicit source download during packaging.
- Preserve existing development packages during real acceptance staging.
- Unknown origin, modified bytes or missing matching notice abort packaging before .deb/AppImage creation.
- Dynamic linkage remains; this work does not choose a new project license or claim complete binary-release acceptance.

## Review focus

- Same path in installed and downloaded packages: choose the actual source tree and verify bytes, never infer a version from a filename.
- Binary rebuild suffixes and source epochs: preserve exact Source identity from package metadata.
- Symlinked Qt files/notices: verify the resolved file belongs to the same source version.
- Missing/modified copyright or common-license files: fail closed instead of writing an incomplete notice set.
- Manifest paths and duplicate destinations: reject escaping/conflicting destinations; no host paths in published records.

### Task 1: Verified Debian origin resolver

**Files:** Create scripts/package_provenance.py and tests/test_package_provenance.py.

**Interfaces:** DebianProvenance(system_root=Path('/'), admin_dir=Path('/var/lib/dpkg'), extracted_root=None, archives=()).stage(origins: dict[str, Path], bundle: Path) -> dict. The result has schemaVersion, components and files; each file has path, sha256, component, and each component has binary/source package/version and notice path/hash.

- [x] Create functional temporary dpkg databases and real tiny .deb fixtures. Assert exact binary/source identities, SHA256, matching notice bytes and no temporary host paths in JSON.
- [x] Observe the new test failing before implementation.
- [x] Implement metadata/hash resolution and notice copying with cached package information. Test modified/unowned files, different rootless versions, symlinks, missing/wrong-version notices, unsafe/conflicting destinations and missing common licenses.
- [x] Run `python3 -m unittest discover -s tests -p test_package_provenance.py -v`; 14 tests pass. Commit the resolver and tests.

### Task 2: Integrate actual staging

**Files:** Modify scripts/package-linux.py, tests/test_linux_packaging.py and CMakeLists.txt.

**Interfaces:** Task 1 stage() consumes the destination-to-source map from all copied Qt/QML/plugin/transitive library files. Add thirdParty to runtime-manifest.json and licenses/debian plus common text files.

- [ ] Extend whole-command fixtures with genuine synthetic package metadata. Assert a copied plugin and its closure have matching notices/manifest; missing origin/notice rejects before output installation artifact.
- [ ] Observe new whole-command failures. Record provenance during tree/file copying and call stage() before constructing .deb/AppImage.
- [ ] Run both packaging suites in CTest. Review final manifest for relative paths only.
- [ ] In a temporary staging root, use the current primary binary and real Qt/runtime closure; build a temporary .deb, inspect all provenance and notice records, smoke-launch AppRun with a clean environment. Preserve prior dist outputs.
- [ ] Document actual source identities, limitations and checks. Obtain independent review, commit and open a draft PR with fresh Linux/Windows CI.

## Remaining distribution gates

A provenance inventory is not corresponding source delivery. Next, collect exact Debian .dsc/original/patch archives and Windows Qt/Hunspell build sources, validate checksums and version identity, and publish them under our release control with build/replacement instructions. Complete Windows runtime notices and redistribution texts. A release must not be labeled accepted until those materials, final installers and the remaining desktop/stability requirements are verified.

References: [Qt obligations](https://www.qt.io/development/open-source-lgpl-obligations), [Debian source package structure](https://www.debian.org/doc/manuals/debian-handbook/sect.source-package-structure.en.html).
