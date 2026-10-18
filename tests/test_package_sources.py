"""Offline source bundle acceptance; no network or user package database."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/package_sources.py'


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / 'input'
        self.inputs.mkdir()
        self.output = self.root / 'output'
        self.manifest = self.root / 'runtime-manifest.json'
        self.request([('example', '2:1.0-3')])
        self.descriptor, self.archive = self.source()

    def request(self, identities):
        self.manifest.write_text(json.dumps({'thirdParty': {'schemaVersion': 1,
            'components': [{'sourcePackage': n, 'sourceVersion': v} for n, v in identities]}}))

    def source(self, name='example', version='2:1.0-3', directory='one', signed=False):
        directory = self.inputs / directory
        directory.mkdir()
        archive = directory / (name + '_1.0.orig.tar.xz')
        archive.write_bytes(b'original source archive\x00\xff\n')
        payload = archive.read_bytes()
        descriptor = directory / (name + '_1.0-3.dsc')
        text = (f'Format: 3.0 (quilt)\nSource: {name}\nVersion: {version}\n'
                'Build-Depends: debhelper-compat (= 13),\n example-dev\n'
                f'Files:\n {hashlib.md5(payload).hexdigest()} {len(payload)} {archive.name}\n'
                f'Checksums-Sha256:\n {hashlib.sha256(payload).hexdigest()} {len(payload)} {archive.name}\n')
        if signed:
            text = ('-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA256\n\n' + text +
                    '-----BEGIN PGP SIGNATURE-----\n\nZmFrZQ==\n-----END PGP SIGNATURE-----\n')
        descriptor.write_text(text)
        return descriptor, archive

    def run_tool(self, succeeds=True):
        result = subprocess.run([sys.executable, str(SCRIPT), '--manifest', str(self.manifest),
            '--input', str(self.inputs), '--output', str(self.output)], capture_output=True, text=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads((self.output / 'source-index.json').read_text())
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('Source bundle rejected:', result.stderr)
        self.assertFalse(self.output.exists())
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ['input', 'runtime-manifest.json'])
        return result

    def test_exact_epoch_archive_bytes_and_manifest_binding(self):
        result = self.run_tool()
        self.assertEqual(result['schemaVersion'], 1)
        self.assertEqual(result['runtimeManifestSha256'], hashlib.sha256(self.manifest.read_bytes()).hexdigest())
        self.assertEqual(len(result['sources']), 1)
        source = result['sources'][0]
        self.assertEqual((source['package'], source['version']), ('example', '2:1.0-3'))
        self.assertEqual(len(source['archives']), 1)
        for item, original in [(source['descriptor'], self.descriptor), (source['archives'][0], self.archive)]:
            self.assertFalse(Path(item['path']).is_absolute())
            self.assertEqual((self.output / item['path']).read_bytes(), original.read_bytes())
            self.assertEqual(item['sha256'], hashlib.sha256(original.read_bytes()).hexdigest())
            self.assertEqual(item['size'], original.stat().st_size)
        self.assertNotIn(str(self.root), json.dumps(result))

    def test_two_versions_and_duplicate_binary_components(self):
        self.request([('example', '2:1.0-3'), ('example', '2:1.0-3'), ('example', '2:1.0-4')])
        self.source(version='2:1.0-4', directory='two')
        result = self.run_tool()
        self.assertEqual([s['version'] for s in result['sources']], ['2:1.0-3', '2:1.0-4'])
        self.assertNotEqual(result['sources'][0]['descriptor']['path'], result['sources'][1]['descriptor']['path'])

    def test_signed_cleartext_descriptor_is_retained_exactly(self):
        self.descriptor.unlink()
        self.archive.unlink()
        self.descriptor.parent.rmdir()
        self.descriptor, self.archive = self.source(signed=True)
        result = self.run_tool()
        self.assertEqual((self.output / result['sources'][0]['descriptor']['path']).read_bytes(), self.descriptor.read_bytes())
        self.assertEqual(result['descriptorAuthentication'], 'not-verified')

    def test_wrong_source_version_is_not_substituted(self):
        self.request([('example', '2:1.0-4')])
        self.run_tool(False)

    def test_missing_archive_cleans_partial_staging(self):
        self.archive.unlink()
        self.run_tool(False)

    def test_changed_archive_same_size_is_rejected(self):
        self.archive.write_bytes(b'x' * self.archive.stat().st_size)
        self.run_tool(False)

    def test_changed_archive_size_is_rejected(self):
        self.archive.write_bytes(self.archive.read_bytes() + b'x')
        self.run_tool(False)

    def test_archive_traversal_is_rejected(self):
        text = self.descriptor.read_text().replace(self.archive.name, '../../outside.tar.xz')
        self.descriptor.write_text(text)
        self.run_tool(False)

    def test_archive_symlink_is_rejected(self):
        self.archive.rename(self.archive.with_suffix('.real'))
        self.archive.symlink_to(self.archive.with_suffix('.real').name)
        self.run_tool(False)

    def test_descriptor_symlink_is_rejected(self):
        self.descriptor.rename(self.descriptor.with_suffix('.real'))
        self.descriptor.symlink_to(self.descriptor.with_suffix('.real').name)
        self.run_tool(False)

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX FIFO fixture')
    def test_archive_fifo_is_rejected_without_reading(self):
        self.archive.unlink()
        os.mkfifo(self.archive)
        result = subprocess.run([sys.executable, str(SCRIPT), '--manifest', str(self.manifest),
            '--input', str(self.inputs), '--output', str(self.output)], capture_output=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_duplicate_control_field_is_rejected(self):
        self.descriptor.write_text(self.descriptor.read_text() + 'Version: 2:1.0-3\n')
        self.run_tool(False)

    def test_duplicate_descriptor_identity_is_rejected(self):
        self.source(directory='two')
        self.run_tool(False)

    def test_sha256_inventory_must_cover_files_field(self):
        text = self.descriptor.read_text().replace('Checksums-Sha256:',
            ' 0123456789abcdef0123456789abcdef 3 omitted.tar.xz\nChecksums-Sha256:')
        self.descriptor.write_text(text)
        self.run_tool(False)

    def test_incomplete_signature_armor_is_rejected(self):
        self.descriptor.write_text('-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA256\n\n' + self.descriptor.read_text())
        self.run_tool(False)

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / 'owner-file'
        marker.write_text('keep me')
        result = subprocess.run([sys.executable, str(SCRIPT), '--manifest', str(self.manifest),
            '--input', str(self.inputs), '--output', str(self.output)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(marker.read_text(), 'keep me')
        self.assertEqual([p.name for p in self.output.iterdir()], ['owner-file'])


    def module(self):
        spec = importlib.util.spec_from_file_location('package_sources', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_atomic_publication_cannot_replace_new_empty_directory(self):
        module = self.module()
        staging = self.root / 'staging'
        staging.mkdir()
        (staging / 'source-file').write_text('source bytes')
        self.output.mkdir()
        before = self.output.stat().st_ino
        with self.assertRaises(FileExistsError):
            module.publish_no_replace(staging, self.output)
        self.assertEqual(self.output.stat().st_ino, before)
        self.assertTrue((staging / 'source-file').exists())
        self.assertEqual(list(self.output.iterdir()), [])

    def test_archive_replaced_with_symlink_after_preflight_is_rejected(self):
        module = self.module()
        outside = self.root / 'outside-source'
        outside.write_bytes(self.archive.read_bytes())
        original = module.regular
        def swap(path):
            original(path)
            path.unlink()
            path.symlink_to(outside)
        with patch.object(module, 'regular', side_effect=swap):
            with self.assertRaises((OSError, ValueError)):
                module.copy_archive(self.archive, self.root / 'copy', outside.stat().st_size,
                    {'sha256': hashlib.sha256(outside.read_bytes()).hexdigest()})

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX FIFO fixture')
    def test_opened_fifo_is_rejected_without_blocking(self):
        module = self.module()
        self.archive.unlink()
        os.mkfifo(self.archive)
        with self.assertRaises((OSError, ValueError)):
            module.open_regular(self.archive)

    def test_descriptor_replaced_with_symlink_after_preflight_is_rejected(self):
        module = self.module()
        outside = self.root / 'outside-descriptor'
        outside.write_bytes(self.descriptor.read_bytes())
        original = module.regular
        def swap(path):
            original(path)
            path.unlink()
            path.symlink_to(outside)
        with patch.object(module, 'regular', side_effect=swap):
            with self.assertRaises((OSError, ValueError)):
                module.read_small(self.descriptor, 10000)

    def test_symlinked_parent_of_input_is_rejected(self):
        alias = self.root / 'alias'
        alias.symlink_to(self.inputs, target_is_directory=True)
        self.inputs = alias / 'one'
        result = subprocess.run([sys.executable, str(SCRIPT), '--manifest', str(self.manifest),
            '--input', str(self.inputs), '--output', str(self.output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('Source bundle rejected:', result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
