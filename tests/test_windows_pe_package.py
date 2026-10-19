"""Small, non-executable PE fixtures for Windows distribution dependency checks."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))

OPTIONAL = 0x98
SECTION = OPTIONAL + 240


def put(data, offset, value, code='I'):
    struct.pack_into('<' + code, data, offset, value)


def pe_fixture(imports=(), delayed=()):
    normal_size = 20 * (len(imports) + 1) if imports else 0
    delay_size = 32 * (len(delayed) + 1) if delayed else 0
    strings = b''.join(name.encode('ascii') + b'\0' for name in (*imports, *delayed))
    raw_size = max(0x600, ((normal_size + delay_size + len(strings) + 511) // 512) * 512)
    data = bytearray(0x200 + raw_size)
    data[:2] = b'MZ'
    put(data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    put(data, 0x84, 0x8664, 'H')
    put(data, 0x86, 1, 'H')
    put(data, 0x94, 240, 'H')
    put(data, OPTIONAL, 0x20b, 'H')
    put(data, OPTIONAL + 24, 0x140000000, 'Q')
    put(data, OPTIONAL + 32, 0x1000)
    put(data, OPTIONAL + 36, 0x200)
    put(data, OPTIONAL + 56, 0x1000 + ((raw_size + 4095) // 4096) * 4096)
    put(data, OPTIONAL + 60, 0x200)
    put(data, OPTIONAL + 108, 16)
    data[SECTION:SECTION + 8] = b'.rdata\0\0'
    put(data, SECTION + 8, raw_size)
    put(data, SECTION + 12, 0x1000)
    put(data, SECTION + 16, raw_size)
    put(data, SECTION + 20, 0x200)
    cursor = normal_size + delay_size
    if imports:
        put(data, OPTIONAL + 112 + 8, 0x1000)
        put(data, OPTIONAL + 112 + 12, normal_size)
    if delayed:
        put(data, OPTIONAL + 112 + 13 * 8, 0x1000 + normal_size)
        put(data, OPTIONAL + 112 + 13 * 8 + 4, delay_size)
    for index, name in enumerate(imports):
        put(data, 0x200 + index * 20 + 12, 0x1000 + cursor)
        put(data, 0x200 + index * 20 + 16, 0x1000)
        encoded = name.encode('ascii') + b'\0'
        data[0x200 + cursor:0x200 + cursor + len(encoded)] = encoded
        cursor += len(encoded)
    for index, name in enumerate(delayed):
        descriptor = 0x200 + normal_size + index * 32
        put(data, descriptor, 1)
        put(data, descriptor + 4, 0x1000 + cursor)
        encoded = name.encode('ascii') + b'\0'
        data[0x200 + cursor:0x200 + cursor + len(encoded)] = encoded
        cursor += len(encoded)
    return data


class WindowsPETest(unittest.TestCase):
    def parse(self, data):
        from windows_pe import parse_imports
        return parse_imports(bytes(data))

    def test_ordinary_delay_and_case_insensitive_unique_names(self):
        result = self.parse(pe_fixture(['Qt6Core.dll', 'KERNEL32.DLL', 'qt6core.DLL'], ['USER32.dll']))
        self.assertEqual(result, {'machine': 'amd64', 'imports': ['kernel32.dll', 'qt6core.dll'],
                                  'delayImports': ['user32.dll']})

    def test_valid_image_without_imports_is_explicitly_empty(self):
        self.assertEqual(self.parse(pe_fixture())['imports'], [])
        self.assertEqual(self.parse(pe_fixture())['delayImports'], [])

    def test_windows_print_spooler_driver_is_a_valid_import_basename(self):
        self.assertEqual(self.parse(pe_fixture(['WINSPOOL.DRV']))['imports'], ['winspool.drv'])

    def test_bad_signatures_architecture_and_headers_are_rejected(self):
        for position, value, code in [(0, 0, 'H'), (0x3c, 0xffffffff, 'I'),
                                      (0x80, 0, 'I'), (0x84, 0x14c, 'H'),
                                      (0x86, 97, 'H'), (0x94, 111, 'H'),
                                      (OPTIONAL, 0x10b, 'H'), (OPTIONAL + 108, 17, 'I')]:
            with self.subTest(position=position):
                data = pe_fixture(['kernel32.dll'])
                put(data, position, value, code)
                with self.assertRaises(ValueError):
                    self.parse(data)
        for length in [0, 63, 0x90, SECTION + 39]:
            with self.subTest(length=length), self.assertRaises(ValueError):
                self.parse(pe_fixture()[:length])

    def test_directory_bounds_and_missing_terminator_cannot_omit_imports(self):
        for rva, size in [(0, 40), (0x1000, 0), (0xfffffffc, 40), (0x1000, 20), (0x1000, 4096)]:
            data = pe_fixture(['kernel32.dll'])
            put(data, OPTIONAL + 120, rva)
            put(data, OPTIONAL + 124, size)
            with self.subTest(rva=rva, size=size), self.assertRaises(ValueError):
                self.parse(data)

    def test_unbacked_virtual_tail_and_outside_raw_bytes_are_rejected(self):
        data = pe_fixture(['kernel32.dll'])
        put(data, SECTION + 8, 0x10000)
        put(data, 0x200 + 12, 0x1900)
        with self.assertRaises(ValueError):
            self.parse(data)
        data = pe_fixture()
        put(data, SECTION + 20, len(data))
        with self.assertRaises(ValueError):
            self.parse(data)

    def test_ambiguous_section_mappings_are_rejected(self):
        for raw_overlap in [False, True]:
            data = pe_fixture(['kernel32.dll'])
            put(data, 0x86, 2, 'H')
            data[SECTION + 40:SECTION + 80] = data[SECTION:SECTION + 40]
            if raw_overlap:
                put(data, SECTION + 40 + 12, 0x3000)
            with self.subTest(raw_overlap=raw_overlap), self.assertRaises(ValueError):
                self.parse(data)

    def test_unsafe_or_unterminated_dll_names_are_rejected(self):
        for name in ['../wrong.dll', r'bad\path.dll', 'C:evil.dll', 'bad name.dll', '', 'a' * 261 + '.dll']:
            with self.subTest(length=len(name)), self.assertRaises(ValueError):
                self.parse(pe_fixture([name]))
        data = pe_fixture(['kernel32.dll'])
        name_offset = 0x200 + 40
        data[name_offset:name_offset + 260] = b'a' * 260
        with self.assertRaises(ValueError):
            self.parse(data)

    def test_delay_flags_and_termination_are_validated(self):
        for flags in [0, 2]:
            data = pe_fixture(delayed=['user32.dll'])
            put(data, 0x200, flags)
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                self.parse(data)
        data = pe_fixture(delayed=['user32.dll'])
        put(data, OPTIONAL + 112 + 13 * 8 + 4, 32)
        with self.assertRaises(ValueError):
            self.parse(data)

    def test_descriptor_count_has_a_fixed_limit(self):
        with self.assertRaises(ValueError):
            self.parse(pe_fixture(['same.dll'] * 2049))

    def test_inspection_hashes_the_parsed_bytes_and_rejects_oversized_file(self):
        from windows_pe import inspect_pe, MAX_PE_BYTES
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'owned.dll'
            data = pe_fixture(['kernel32.dll'])
            path.write_bytes(data)
            result = inspect_pe(path)
            self.assertEqual(result['bytes'], len(data))
            self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())
            with path.open('wb') as stream:
                stream.truncate(MAX_PE_BYTES + 1)
            with self.assertRaises(ValueError):
                inspect_pe(path)


class WindowsDependencyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage, self.system = self.root / 'stage', self.root / 'system'
        self.stage.mkdir()
        self.system.mkdir()

    def write(self, root, name, imports=(), delayed=()):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(pe_fixture(imports, delayed))
        return path

    def manifest(self):
        from package_windows_audit import inventory
        return {p.relative_to(self.stage).as_posix(): inventory(p)
                for p in self.stage.rglob('*') if p.is_file()}

    def collect(self, manifest=None):
        from package_windows_dependencies import collect_dependencies
        return collect_dependencies(self.stage, self.manifest() if manifest is None else manifest,
                                    self.system)

    def test_staged_and_explicit_system_dependencies_are_portable(self):
        self.write(self.stage, 'reword.exe', ['Qt6Core.DLL'])
        self.write(self.stage, 'Qt6Core.dll', ['kernel32.dll'])
        self.write(self.system, 'KERNEL32.DLL')
        report = self.collect()
        self.assertTrue(report['ordinaryClosureComplete'])
        self.assertEqual(report['dependencies']['qt6core.dll']['kind'], 'stage')
        self.assertEqual(report['dependencies']['kernel32.dll']['kind'], 'system')
        self.assertEqual(report['dependencies']['kernel32.dll']['candidates'], ['KERNEL32.DLL'])
        self.assertNotIn(str(self.root), repr(report))
        self.assertFalse(report['releaseReady'])

    def test_missing_ordinary_and_delayed_imports_are_both_reported(self):
        self.write(self.stage, 'reword.exe', ['missing.dll'], ['optional.dll'])
        report = self.collect()
        self.assertFalse(report['ordinaryClosureComplete'])
        self.assertEqual(report['missingImports'], ['missing.dll', 'optional.dll'])

    def test_system_driver_import_is_checked_by_exact_bytes(self):
        self.write(self.stage, 'reword.exe', ['WINSPOOL.DRV'])
        self.write(self.system, 'winspool.drv')
        report = self.collect()
        self.assertTrue(report['ordinaryClosureComplete'])
        self.assertEqual(report['dependencies']['winspool.drv']['kind'], 'system')
        self.assertEqual(report['dependencies']['winspool.drv']['bytes'],
                         (self.system / 'winspool.drv').stat().st_size)

    def test_api_contracts_are_separate_from_resolved_system_files(self):
        self.write(self.stage, 'reword.exe', ['api-ms-win-core-file-l1-1-0.dll'])
        report = self.collect()
        self.assertEqual(report['apiSetContracts'], ['api-ms-win-core-file-l1-1-0.dll'])
        self.assertEqual(report['apiSetHostCoverage'], 'not-established')
        self.assertEqual(report['dynamicLoadingCoverage'], 'not-established')
        self.assertFalse(report['releaseReady'])

    def test_differing_duplicate_basenames_are_ambiguous(self):
        self.write(self.stage, 'reword.exe', ['same.dll'])
        self.write(self.stage, 'a/same.dll')
        other = self.write(self.stage, 'b/SAME.dll')
        with other.open('ab') as stream:
            stream.write(b'different build bytes')
        report = self.collect()
        self.assertFalse(report['ordinaryClosureComplete'])
        self.assertEqual(report['ambiguousImports'], ['same.dll'])

    def test_identical_duplicate_basenames_keep_all_candidates(self):
        self.write(self.stage, 'reword.exe', ['same.dll'])
        for name in ['a/same.dll', 'b/SAME.dll']:
            self.write(self.stage, name)
        report = self.collect()
        self.assertTrue(report['ordinaryClosureComplete'])
        self.assertEqual(report['dependencies']['same.dll']['candidates'], ['a/same.dll', 'b/SAME.dll'])

    def test_changed_binary_invalidates_saved_inventory(self):
        path = self.write(self.stage, 'reword.exe')
        manifest = self.manifest()
        with path.open('ab') as stream:
            stream.write(b'changed')
        with self.assertRaises(ValueError):
            self.collect(manifest)

    def test_manifest_cannot_escape_stage(self):
        external = self.write(self.root, 'external.dll')
        from package_windows_audit import inventory
        with self.assertRaises(ValueError):
            self.collect({'../external.dll': inventory(external)})

    def test_malformed_saved_hash_record_is_rejected_cleanly(self):
        self.write(self.stage, 'reword.exe')
        for record in [{}, None, {'bytes': 2048, 'sha256': 'bad'}]:
            with self.subTest(record=record), self.assertRaises(ValueError):
                self.collect({'reword.exe': record})

    def test_linked_system_input_is_rejected(self):
        external = self.write(self.root, 'external.dll')
        self.write(self.stage, 'reword.exe', ['kernel32.dll'])
        try:
            (self.system / 'kernel32.dll').symlink_to(external)
        except OSError:
            self.skipTest('Account cannot create symlinks')
        with self.assertRaises(ValueError):
            self.collect()

    def test_nested_link_cannot_escape_the_stage_with_matching_bytes(self):
        external = self.write(self.root / 'outside', 'external.dll')
        from package_windows_audit import inventory
        try:
            (self.stage / 'nested').symlink_to(external.parent, target_is_directory=True)
        except OSError:
            self.skipTest('Account cannot create symlinks')
        with self.assertRaises(ValueError):
            self.collect({'nested/external.dll': inventory(external)})

    def test_audit_can_include_exact_dependency_records(self):
        import package_windows_audit
        self.write(self.stage, 'reword.exe', ['kernel32.dll'])
        self.write(self.system, 'kernel32.dll')
        vendor = self.root / 'vendor'
        vendor.mkdir()
        output = self.root / 'audit'
        report = package_windows_audit.audit(self.stage, self.stage / 'reword.exe',
                                            {'qt': vendor}, output, system=self.system)
        self.assertTrue(report['dependencyClosure']['ordinaryClosureComplete'])
        self.assertTrue((output / 'audit.json').is_file())

    def cli(self):
        import subprocess
        vendor = self.root / 'vendor'
        vendor.mkdir()
        output = self.root / 'audit'
        result = subprocess.run([sys.executable,
                                 str(Path(__file__).resolve().parents[1] / 'scripts/package_windows_audit.py'),
                                 '--stage', str(self.stage), '--application', str(self.stage / 'reword.exe'),
                                 '--vendor', 'qt=' + str(vendor), '--output', str(output),
                                 '--system-directory', str(self.system)],
                                capture_output=True, text=True, timeout=5)
        return result, output

    def test_real_cli_rejects_missing_import_but_keeps_failed_evidence(self):
        import json
        self.write(self.stage, 'reword.exe', ['missing.dll'])
        result, output = self.cli()
        self.assertEqual(result.returncode, 1)
        report = json.loads((output / 'audit.json').read_text())
        self.assertFalse(report['dependencyClosure']['ordinaryClosureComplete'])
        self.assertEqual(report['dependencyClosure']['missingImports'], ['missing.dll'])

    def test_invalid_pe_retains_an_explicit_failed_collection_record(self):
        import json
        self.write(self.stage, 'reword.exe')
        (self.stage / 'broken.dll').write_bytes(b'MZ')
        result, output = self.cli()
        self.assertEqual(result.returncode, 1)
        report = json.loads((output / 'audit.json').read_text())
        closure = report['dependencyClosure']
        self.assertFalse(closure['ordinaryClosureComplete'])
        self.assertFalse(closure['collectionComplete'])
        self.assertEqual(closure['failure'], 'invalid-or-unavailable-pe-metadata')


if __name__ == '__main__':
    unittest.main()
