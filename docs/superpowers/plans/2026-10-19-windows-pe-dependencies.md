# Windows PE dependency evidence implementation plan

> For agentic workers: execute inline using executing-plans; independent whole-branch review before integration. User authorization covers continued implementation without further plan approval.

**Goal:** Detect missing ordinary DLL dependencies in the actual Windows installer stage without loading executable code.

**Architecture:** A bounded standard-library PE32+ reader extracts ordinary and delayed import DLL names. The existing material audit maps those imports to staged files or an explicit Windows system directory, keeping API-set contracts separate. The packager fails on unresolved ordinary DLLs and retains a portable report outside the installer.

**Tech stack:** Python standard library, existing PowerShell packager, hosted Linux/Windows CI.

**Spec:** docs/REQUIREMENTS.md and docs/WINDOWS_DISTRIBUTION.md. Format reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

## Global constraints

- Windows x64 only; no DLL loading, subprocess execution of audited binaries or inferred source/license completeness.
- Read at most 128 MiB per PE, 96 sections, 2,048 import descriptors per table and 260 bytes per DLL name; reject malformed, ambiguous or truncated mappings.
- Report relative paths/hashes only. Do not copy build metadata containing machine paths.
- A resolved current-runner file does not prove Windows 10/11 compatibility. API-set contracts and runtime LoadLibrary calls remain explicit coverage limits.
- Preserve existing material audit callers unless dependency collection is explicitly requested; the real Windows packager requests it.
- After two host hard freezes: no local builds or GUI/heavy tests. Tiny parser tests may run with explicit address-space/CPU limits; complete verification is hosted CI.

## Review focus

- Truncated tables, invalid RVA and zero-filled section tails must not silently omit dependencies.
- Delay-load tables and case-insensitive DLL names must be covered.
- Same basename with different staged bytes is ambiguous, not a successful match.
- System-directory names cannot hide a missing bundled Qt/Hunspell DLL through a developer PATH.
- A changed binary between inventory and parsing must invalidate publication.

### Task 1: Bounded import reader

Files: scripts/windows_pe.py; tests/test_windows_pe_package.py.
Interface: parse_imports(data: bytes) -> {machine, imports, delayImports}; inspect_pe(path: Path) -> that record plus bytes/sha256.

- [x] Write minimal PE32+ fixtures and observe RED for normal/delay imports, empty imports, malformed signature/machine/optional header, bad section ranges, table termination, unsafe names and byte/descriptor limits.
- [x] Implement explicit byte/RVA bounds and exact AMD64 PE32+ validation; no fallback to empty imports on parse errors.
- [x] Verify bounded unit tests; commit the coherent parser.

### Task 2: Stage dependency report and real packaging

Files: scripts/package_windows_dependencies.py; scripts/package_windows_audit.py; scripts/package-windows.ps1; tests/test_windows_pe_package.py.
Interface: collect_dependencies(stage: Path, inventory: dict, system: Path) -> portable dependency report. Audit's optional system-directory argument enables strict ordinary closure collection.

- [x] Observe RED for staged/system matches, missing DLL, differing duplicate basename, missing delay import, API-set separation, binary mutation and linked input rejection.
- [x] Implement exact-byte records and resolution using only stage and explicit system directory; include unresolved/ambiguous imports and coverage limits.
- [x] Enable it in the real Windows packager; unresolved ordinary imports stop installer creation. Keep source/notice and product release readiness false.
- [ ] Obtain hosted full suite and actual Windows artifact, inspect the DLL map and any failures; resolve actual defects rather than weaken missing-dependency checks.

### Task 3: Review and integration evidence

- [ ] Independent code review; reproduce/fix important findings, with fresh hosted tests.
- [ ] Record exact source/run/artifact/hash results and remaining API-set, dynamic-loading, source/notice and normal-desktop gates in WINDOWS_DISTRIBUTION and CURRENT_HANDOFF.
- [ ] Publish the reviewed branch/PR; attach created PR. Do not release/install or shut down with owned work still active.

Ruling: use the existing clean managed storage-access worktree on codex/windows-pe-dependencies, based on origin/develop. Keep PR18's native stability work separate. Full baseline evidence is the recorded passing CI at the unchanged base; rebuilding locally is suspended after hard freezes. This plan is a required distribution-evidence increment, not a narrowed replacement for the full product scope.

Task 1 evidence: missing parser was RED; all ten reader tests now pass in
0.021 s under `prlimit --as=268435456 --cpu=10` and low scheduling priority.
Only small non-executable fixtures are parsed; no local build/GUI is started.
The full suite and actual vendor PE parsing still require hosted CI.

Task 2 working evidence: missing collector and optional audit argument/CLI were
observed RED, as were malformed saved hash records. All 22 bounded fixture/CLI
tests pass in 0.079 s. The real CLI keeps a failed audit report while returning
nonzero for a missing DLL. Windows CI now runs these tests and retains material
artifacts even if packaging fails. Actual stage resolution and full CI remain
pending; fixture success does not establish closure of the real installer.
