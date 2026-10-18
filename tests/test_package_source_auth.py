"""Authenticate real signed fixture descriptors and archive index hash chains."""
import hashlib
import importlib
import json
import lzma
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts'
sys.path.insert(0, str(SCRIPTS))


class SourceAuthenticationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.home = cls.root / 'gnupg'
        cls.home.mkdir(mode=0o700)
        cls.gpg('--quick-generate-key', 'Reword source fixture', 'ed25519', 'sign', '0')
        cls.keyring = cls.gpg('--export')

    @classmethod
    def tearDownClass(cls):
        subprocess.run(['gpgconf', '--homedir', str(cls.home), '--kill', 'gpg-agent'], capture_output=True)
        cls.temp.cleanup()

    @classmethod
    def gpg(cls, *args, data=None):
        process = subprocess.run(['gpg', '--homedir', str(cls.home), '--batch', '--yes',
            '--pinentry-mode', 'loopback', '--passphrase', '', *args], input=data, capture_output=True)
        if process.returncode:
            raise RuntimeError(process.stderr.decode(errors='replace'))
        return process.stdout

    def setUp(self):
        self.case = tempfile.TemporaryDirectory()
        self.addCleanup(self.case.cleanup)
        self.directory = Path(self.case.name)
        self.config = self.directory / 'trust.json'
        (self.directory / 'keyring.gpg').write_bytes(self.keyring)
        self.settings = {'schemaVersion': 1, 'maintainerKeyrings': ['keyring.gpg'],
                         'archiveKeyrings': ['keyring.gpg'], 'indexes': []}
        self.plain = b'Format: 3.0 (quilt)\nSource: example\nVersion: 2:1.0-3\n'
        self.signed = self.sign(self.plain)
        self.descriptors = {('example', '2:1.0-3'): ('example_1.0-3.dsc', self.signed)}

    def sign(self, data):
        return self.gpg('--armor', '--digest-algo', 'SHA256', '--clearsign', data=data)

    def index(self, data=None, name='main', version='2:1.0-3'):
        if data is None:
            data = self.signed
        contents = (f'Package: example\nVersion: {version}\nChecksums-Sha256:\n '
            f'{hashlib.sha256(data).hexdigest()} {len(data)} example_1.0-3.dsc\n\n').encode()
        self.index_bytes(contents, name)

    def index_bytes(self, contents, name='main', source_path='main/source/Sources.xz',
                    expanded_size=None, expanded_digest=None, compressed=None):
        if compressed is None:
            compressed = lzma.compress(contents)
        if expanded_size is None:
            expanded_size = len(contents)
        if expanded_digest is None:
            expanded_digest = hashlib.sha256(contents).hexdigest()
        release = ('Origin: Debian\nCodename: fixture\nDate: Thu, 01 Jan 2026 00:00:00 UTC\nSHA256:\n '
            f'{hashlib.sha256(compressed).hexdigest()} {len(compressed)} {source_path}\n '
            f'{expanded_digest} {expanded_size} {source_path[:-3]}\n').encode()
        (self.directory / (name + '.InRelease')).write_bytes(self.sign(release))
        (self.directory / (name + '.xz')).write_bytes(compressed)
        self.settings['indexes'].append({'release': name + '.InRelease', 'sources': name + '.xz', 'path': source_path})

    def authenticate(self):
        self.config.write_text(json.dumps(self.settings))
        module = importlib.import_module('package_source_auth')
        return module.authenticate(self.config, self.descriptors)

    def test_direct_maintainer_signature_and_no_host_paths(self):
        result = self.authenticate()
        self.assertEqual(result['schemaVersion'], 1)
        source = result['sources'][0]
        self.assertEqual((source['package'], source['version'], source['method']),
                         ('example', '2:1.0-3', 'maintainer-signature'))
        self.assertEqual(source['descriptorSha256'], hashlib.sha256(self.signed).hexdigest())
        self.assertRegex(source['signers'][0], r'^[A-F0-9]{40,64}$')
        self.assertEqual(result['keyrings'][0]['sha256'], hashlib.sha256(self.keyring).hexdigest())
        self.assertNotIn(str(self.directory), json.dumps(result))

    def test_archive_chain_accepts_exact_descriptor_without_maintainer_root(self):
        self.index()
        self.settings['maintainerKeyrings'] = []
        result = self.authenticate()
        self.assertEqual(result['sources'][0]['method'], 'archive-index')
        self.assertEqual(result['indexes'][0]['codename'], 'fixture')
        self.assertEqual(result['indexes'][0]['inReleaseSha256'],
                         hashlib.sha256((self.directory / 'main.InRelease').read_bytes()).hexdigest())

    def test_unknown_maintainer_root_is_rejected(self):
        self.settings['maintainerKeyrings'] = []
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_modified_signature_is_rejected(self):
        self.descriptors[('example', '2:1.0-3')] = ('example_1.0-3.dsc', self.signed.replace(b'Format: 3.0', b'Format: 1.0'))
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_requested_version_must_match_signed_identity(self):
        self.descriptors = {('example', '2:1.0-4'): ('example_1.0-3.dsc', self.signed)}
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_changed_release_is_rejected(self):
        self.index()
        path = self.directory / 'main.InRelease'
        path.write_bytes(path.read_bytes().replace(b'Codename: fixture', b'Codename: altered'))
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_changed_compressed_index_is_rejected(self):
        self.index()
        path = self.directory / 'main.xz'
        path.write_bytes(path.read_bytes() + b'extra')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_no_fallback_for_descriptor_disagreement_with_authenticated_identity(self):
        self.index(data=self.signed + b'\n')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_conflicting_authenticated_indexes_are_rejected(self):
        self.index()
        self.index(data=self.signed + b'\n', name='second')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_missing_historical_version_uses_explicit_maintainer_root(self):
        self.index(version='2:1.0-4')
        self.assertEqual(self.authenticate()['sources'][0]['method'], 'maintainer-signature')

    def test_archive_index_without_trusted_archive_root_is_rejected(self):
        self.index()
        self.settings['archiveKeyrings'] = []
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_unsafe_signed_index_path_is_rejected(self):
        self.index_bytes(b'', source_path='../Sources.xz')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_duplicate_source_paragraph_identity_is_rejected(self):
        row = (f'Package: example\nVersion: 2:1.0-3\nChecksums-Sha256:\n '
            f'{hashlib.sha256(self.signed).hexdigest()} {len(self.signed)} example_1.0-3.dsc\n\n').encode()
        self.index_bytes(row + row)
        with self.assertRaises(ValueError):
            self.authenticate()


    def test_uncompressed_index_digest_must_match_signed_release(self):
        self.index_bytes(b'Package: unrelated\n\n', expanded_digest='0' * 64)
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_signed_expanded_size_limit_is_checked_before_decompression(self):
        self.index_bytes(b'Package: unrelated\n\n', expanded_size=512 * 1024**2 + 1)
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_signed_invalid_xz_is_rejected_as_invalid_input(self):
        self.index_bytes(b'Package: unrelated\n\n', compressed=b'invalid xz bytes')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_keyring_symlink_cannot_change_trust_input(self):
        path = self.directory / 'keyring.gpg'
        path.rename(self.directory / 'actual.gpg')
        path.symlink_to('actual.gpg')
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_expired_signing_certificate_is_not_accepted_from_validsig_alone(self):
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)
            home.chmod(0o700)
            command = ['gpg', '--homedir', str(home), '--batch', '--yes', '--pinentry-mode',
                       'loopback', '--passphrase', '', '--faked-system-time', '946684800']
            try:
                subprocess.run(command + ['--quick-generate-key', 'Expired source fixture',
                    'ed25519', 'sign', '1d'], capture_output=True, check=True)
                signature = subprocess.run(command + ['--armor', '--clearsign'], input=self.plain,
                    capture_output=True, check=True).stdout
                keyring = subprocess.run(command + ['--export'], capture_output=True, check=True).stdout
            finally:
                subprocess.run(['gpgconf', '--homedir', str(home), '--kill', 'gpg-agent'], capture_output=True)
        (self.directory / 'expired.gpg').write_bytes(keyring)
        self.settings['maintainerKeyrings'] = ['expired.gpg']
        self.descriptors[('example', '2:1.0-3')] = ('example_1.0-3.dsc', signature)
        with self.assertRaises(ValueError):
            self.authenticate()

    def test_assembly_requires_configured_authentication_before_publication(self):
        self.settings['maintainerKeyrings'] = []
        self.config.write_text(json.dumps(self.settings))
        archive = b'fixture archive\n'
        plain = self.plain + (f'Checksums-Sha256:\n {hashlib.sha256(archive).hexdigest()} '
                             f'{len(archive)} example.orig.tar.xz\n').encode()
        inputs = self.directory / 'input'
        inputs.mkdir()
        (inputs / 'example.dsc').write_bytes(self.sign(plain))
        (inputs / 'example.orig.tar.xz').write_bytes(archive)
        manifest = self.directory / 'runtime.json'
        manifest.write_text(json.dumps({'thirdParty': {'schemaVersion': 1, 'components':
            [{'sourcePackage': 'example', 'sourceVersion': '2:1.0-3'}]}}))
        output = self.directory / 'output'
        command = [sys.executable, str(SCRIPTS / 'package_sources.py'), '--manifest', str(manifest),
            '--input', str(inputs), '--output', str(output), '--trust-config', str(self.config)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.settings['maintainerKeyrings'] = ['keyring.gpg']
        self.config.write_text(json.dumps(self.settings))
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        index = json.loads((output / 'source-index.json').read_text())
        self.assertEqual(index['descriptorAuthentication'], 'verified')
        self.assertEqual(len(index['authentication']['sources']), 1)


if __name__ == '__main__':
    unittest.main()
