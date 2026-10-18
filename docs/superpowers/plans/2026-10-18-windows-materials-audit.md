# Audit Windows distribution materials

The approved installer must be self-contained, with exact third-party material
rather than a generic project link. Current CI uses Qt 6.8.3, Hunspell 1.7.5 and
VC143 runtime DLLs. Linux's Debian source bundle does not match these binaries.

1. Add a portable read-only audit with synthetic regression tests first. Hash
   every staged file, match executable/DLL bytes to explicit Qt/vcpkg/CRT roots,
   and retain vendor notice/build metadata under relative vendor paths. Unknown
   binaries and missing notices must remain explicit; this is an audit, not a
   release approval or source-authentication substitute.
2. Invoke it from Windows packaging with the exact roots already used for
   deployment. Publish its evidence as a separate CI artifact. Do not silently
   change deployed plugins or graphics behavior as part of this audit.
3. Inspect the actual artifact, then bind exact source/version/patch/build inputs
   and decide which unused components can safely be omitted in a later change.
4. Run the complete test suite, fresh Windows installer acceptance and independent
   review before integrating the tool. Public binary/source pairing and ordinary
   Windows 10/11 desktop acceptance remain separate release gates.

Official references:
- https://doc.qt.io/qt-6.8/windows-deployment.html
- https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170

Pinned source candidates and official SDK/source metadata are retained in ignored
.deps/windows-source-acquisition. Their collection does not establish a complete
mapping to deployed SDK bytes or a complete VC runtime redistribution inventory.
