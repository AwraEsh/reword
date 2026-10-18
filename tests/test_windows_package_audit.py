"""Functional fixtures for Windows deployment evidence; no PE execution."""
import json
import os
from unittest.mock import patch
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))


class WindowsAuditTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.stage = self.base / 'stage'
        self.stage.mkdir()
        self.vendor = self.base / 'sdk'
        (self.vendor / 'bin').mkdir(parents=True)
        self.app = self.base / 'app.exe'
        self.app.write_bytes(b'opaque application fixture')
        (self.stage / 'reword.exe').write_bytes(self.app.read_bytes())
        self.out = self.base / 'evidence'

    def audit(self):
        import package_windows_audit
        return package_windows_audit.audit(self.stage, self.app, {'qt': self.vendor}, self.out)

    def test_matches_bytes_and_preserves_relative_vendor_notices(self):
        (self.vendor / 'bin/Qt6Core.dll').write_bytes(b'opaque library fixture')
        (self.stage / 'Qt6Core.dll').write_bytes(b'opaque library fixture')
        (self.vendor / 'LICENSES').mkdir()
        (self.vendor / 'LICENSES/LGPL-3.0.txt').write_text('Synthetic vendor terms')
        report = self.audit()
        self.assertEqual(report['binaries']['Qt6Core.dll']['matches'], ['qt/bin/Qt6Core.dll'])
        self.assertEqual(report['binaries']['reword.exe']['matches'], ['application'])
        self.assertEqual((self.out / 'notices/qt/LICENSES/LGPL-3.0.txt').read_text(), 'Synthetic vendor terms')
        self.assertFalse(report['releaseReady'])
        self.assertNotIn(str(self.base), json.dumps(report))

    def test_same_name_different_bytes_is_unmatched(self):
        (self.vendor / 'bin/Qt6Core.dll').write_bytes(b'original')
        (self.stage / 'Qt6Core.dll').write_bytes(b'patched')
        report = self.audit()
        self.assertEqual(report['unmatchedBinaries'], ['Qt6Core.dll'])
        self.assertEqual(report['missingVendorNotices'], ['qt'])

    def test_duplicate_names_keep_all_exact_candidates(self):
        (self.vendor / 'plugins').mkdir()
        for parent in ['bin', 'plugins']:
            (self.vendor / parent / 'same.dll').write_bytes(b'same')
        (self.stage / 'same.dll').write_bytes(b'same')
        report = self.audit()
        self.assertEqual(report['binaries']['same.dll']['matches'], ['qt/bin/same.dll', 'qt/plugins/same.dll'])

    def test_application_tampering_is_not_accepted_as_owned(self):
        (self.stage / 'reword.exe').write_bytes(b'changed')
        report = self.audit()
        self.assertEqual(report['unmatchedBinaries'], ['reword.exe'])

    def test_existing_output_is_preserved(self):
        self.out.mkdir()
        (self.out / 'keep').write_bytes(b'untouched')
        with self.assertRaises(ValueError):
            self.audit()
        self.assertEqual((self.out / 'keep').read_bytes(), b'untouched')

    def test_stage_symlink_is_refused_before_publication(self):
        try:
            (self.stage / 'linked.dll').symlink_to(self.app)
        except OSError:
            self.skipTest('Account cannot create symlinks')
        with self.assertRaises(ValueError):
            self.audit()
        self.assertFalse(self.out.exists())

    def test_unreadable_subdirectory_cannot_disappear_from_inventory(self):
        blocked = self.stage / 'blocked'
        blocked.mkdir()
        (blocked / 'hidden.dll').write_bytes(b'unmatched fixture')
        real_scan = os.scandir
        def scan(path):
            if Path(path) == blocked:
                raise PermissionError('Synthetic denied-directory traversal')
            return real_scan(path)
        with patch('os.scandir', side_effect=scan):
            with self.assertRaises(PermissionError):
                self.audit()
        self.assertFalse(self.out.exists())

    def test_large_notice_is_refused_without_partial_output(self):
        with (self.vendor / 'LICENSE.txt').open('wb') as f:
            f.truncate(4 * 1024 * 1024 + 1)
        with self.assertRaises(ValueError):
            self.audit()
        self.assertFalse(self.out.exists())


if __name__ == '__main__':
    unittest.main()
