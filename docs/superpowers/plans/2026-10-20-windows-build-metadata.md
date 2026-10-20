# Retain bounded Windows build metadata

Goal: make the existing vendor audit inspectable for final source/ABI matching.
The current report records hashes of Qt/vcpkg build metadata but retains only
notice contents. Hashes alone cannot reveal the cached package's recorded source
or ABI inputs. This remains development evidence, not redistribution approval.

Reuse the clean storage-access managed worktree on codex/windows-build-metadata,
based on the verified native-guard branch. Keep the UI changes isolated in PR22.
No local build, GUI or heavy test. Tiny Python fixtures may run under 256 MiB and
10 CPU seconds; full integration and actual Windows materials run on hosted CI.

- Add functional RED cases requiring exact retained metadata bytes at their safe
  relative vendor names; altered input must not leave a partial successful audit.
- Bound metadata independently: 1 MiB per file and 8 MiB total. Continue accepting
  only the existing explicit metadata filename whitelist and ordinary vendor
  files. Refuse symlink/junction/non-regular inputs and existing output roots.
- Retain metadata under build-metadata alongside notices. Verify each copied file
  against its recorded size/hash; clean owned output on any failure. Do not copy
  arbitrary vendor files, environments, credentials or runtime/user data.
- Verify tiny RED/GREEN, full hosted tests, real Windows audit and independent
  review. Inspect the retained vcpkg SPDX/ABI and Qt metadata before asserting any
  cache/source binding. Checkout identity and an SPDX record alone do not prove
  the compiled payload matches authenticated source.
- Update the handoff with actual identities and remaining notices/source gates.
  No product release or local installation follows from this task alone.

Observed local RED under 256 MiB/10 CPU seconds: 12 functional fixtures in
0.090 seconds, eight existing controls pass and four new cases fail as expected
(no retained metadata, no per-file/aggregate cap, no changed-input rejection).
Failure log is ignored at .deps/windows-metadata-red.log. No C++ build or GUI ran.

Local GREEN: all 12 Windows-audit fixtures pass in 0.186 seconds under 256 MiB
and 10 CPU seconds. Metadata is retained under build-metadata with the existing
safe vendor-relative names and exact recorded hashes. Per-file and aggregate
bounds reject before output publication; changed copies remove the owned partial
audit. The releaseReady/source-authentication fields remain unchanged and false.
Full hosted tests and real Windows material inspection are still pending.
