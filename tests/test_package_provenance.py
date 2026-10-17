"""Functional provenance checks with private package databases and tiny .deb files."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from package_provenance import DebianProvenance


class ProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='reword-provenance-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.system = self.root / 'system'
        self.admin = self.root / 'dpkg'
        self.extracted = self.root / 'extracted'
        self.bundle = self.root / 'bundle'
        (self.admin / 'info').mkdir(parents=True)
        (self.admin / 'status').write_text('')
        (self.admin / 'info/format').write_text('1\n')
        common = self.system / 'usr/share/common-licenses'
        common.mkdir(parents=True)
        self.install('base-files', '1.0', 'base-files', {
            'usr/share/common-licenses/LGPL-3': b'synthetic common license\n'})
        self.notice = b'Synthetic copyright\nSee /usr/share/common-licenses/LGPL-3.\n'
        self.install('libfixture1', '1.2-3+b1', 'fixture (1.2-3)', {
            'usr/lib/libfixture.so.1': b'\x7fELFsynthetic library',
            'usr/share/doc/libfixture1/copyright': self.notice,
        })

    def install(self, name, version, source, files):
        control = (f'Package: {name}\nStatus: install ok installed\nArchitecture: amd64\nMulti-Arch: same\n'
                   f'Version: {version}\nSource: {source}\nMaintainer: Fixture <fixture@example.invalid>\nDescription: synthetic fixture\n\n')
        with (self.admin / 'status').open('a') as stream:
            stream.write(control)
        sums = []
        for relative, data in files.items():
            path = self.system / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            sums.append(hashlib.md5(data).hexdigest() + '  ' + relative)
        info = self.admin / 'info' / (name + ':amd64')
        info.with_suffix(info.suffix + '.md5sums').write_text('\n'.join(sums) + '\n')
        info.with_suffix(info.suffix + '.list').write_text(''.join('/' + p + '\n' for p in files))

    def archive(self, name='libfixture1', version='1.2-4+b2', source='fixture (1.2-4)'):
        folder = self.root / ('archive-' + name)
        (folder / 'DEBIAN').mkdir(parents=True)
        (folder / 'DEBIAN/control').write_text(
            f'Package: {name}\nVersion: {version}\nSource: {source}\nArchitecture: amd64\n'
            'Maintainer: Synthetic Fixture <fixture@example.invalid>\nDescription: test fixture\n')
        sums = []
        for relative, data in {
            'usr/lib/libfixture.so.1': b'\x7fELFrootless different version',
            f'usr/share/doc/{name}/copyright': self.notice,
        }.items():
            target = folder / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            sums.append(hashlib.md5(data).hexdigest() + '  ' + relative)
        (folder / 'DEBIAN/md5sums').write_text('\n'.join(sums) + '\n')
        archive = self.root / (name + '.deb')
        subprocess.run(['dpkg-deb', '--build', str(folder), str(archive)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        subprocess.run(['dpkg-deb', '-x', str(archive), str(self.extracted)], check=True)
        return archive

    def collector(self, archives=()):
        return DebianProvenance(system_root=self.system, admin_dir=self.admin,
                                extracted_root=self.extracted, archives=archives)

    def stage(self, path=None, collector=None):
        path = path or self.system / 'usr/lib/libfixture.so.1'
        destination = 'usr/lib/libfixture.so.1'
        target = self.bundle / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        return (collector or self.collector()).stage({destination: path}, self.bundle)

    def test_installed_bytes_source_identity_and_notice_are_exact(self):
        result = self.stage()
        self.assertEqual(result['schemaVersion'], 1)
        component = result['components'][0]
        self.assertEqual(component['package'], 'libfixture1:amd64')
        self.assertEqual(component['version'], '1.2-3+b1')
        self.assertEqual(component['sourcePackage'], 'fixture')
        self.assertEqual(component['sourceVersion'], '1.2-3')
        notice = self.bundle / component['notice']
        self.assertEqual(notice.read_bytes(), self.notice)
        self.assertEqual(component['noticeSha256'], hashlib.sha256(self.notice).hexdigest())
        self.assertEqual(result['files'][0]['sha256'], hashlib.sha256(
            (self.system / 'usr/lib/libfixture.so.1').read_bytes()).hexdigest())
        self.assertNotIn(str(self.root), json.dumps(result))
        self.assertTrue((self.bundle / 'usr/share/reword/licenses/common/LGPL-3').exists())

    def test_rootless_archive_takes_its_own_version_not_installed_version(self):
        archive = self.archive()
        result = self.stage(self.extracted / 'usr/lib/libfixture.so.1', self.collector([archive]))
        component = result['components'][0]
        self.assertEqual(component['version'], '1.2-4+b2')
        self.assertEqual(component['sourceVersion'], '1.2-4')
        self.assertEqual(component['archiveSha256'], hashlib.sha256(archive.read_bytes()).hexdigest())

    def test_modified_runtime_file_is_rejected(self):
        path = self.system / 'usr/lib/libfixture.so.1'
        path.write_bytes(b'changed after package installation')
        with self.assertRaisesRegex(RuntimeError, 'checksum'):
            self.stage(path)

    def test_source_epoch_is_preserved_independently_of_binary_rebuild(self):
        self.install('libepoch1', '2:1.2-3+b1', 'epoch (2:1.2-3)', {
            'usr/lib/libepoch.so': b'synthetic epoch library',
            'usr/share/doc/libepoch1/copyright': self.notice})
        component = self.stage(self.system / 'usr/lib/libepoch.so')['components'][0]
        self.assertEqual(component['version'], '2:1.2-3+b1')
        self.assertEqual(component['sourceVersion'], '2:1.2-3')

    def test_ambiguous_rootless_versions_are_rejected(self):
        archive = self.archive()
        first = self.root / 'first-version.deb'
        shutil.copyfile(archive, first)
        control = self.root / 'archive-libfixture1/DEBIAN/control'
        control.write_text(control.read_text().replace('1.2-4', '1.2-5'))
        subprocess.run(['dpkg-deb', '--build', str(control.parent.parent), str(archive)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        with self.assertRaisesRegex(RuntimeError, 'ambiguous'):
            self.stage(self.extracted / 'usr/lib/libfixture.so.1', self.collector([first, archive]))

    def test_unowned_file_and_unindexed_rootless_file_are_rejected(self):
        path = self.system / 'usr/lib/unowned.so'
        path.write_bytes(b'unknown')
        with self.assertRaisesRegex(RuntimeError, 'origin'):
            self.stage(path)
        self.archive()
        with self.assertRaisesRegex(RuntimeError, 'origin'):
            self.stage(self.extracted / 'usr/lib/libfixture.so.1')

    def test_missing_or_modified_notice_is_rejected(self):
        copyright = self.system / 'usr/share/doc/libfixture1/copyright'
        copyright.unlink()
        with self.assertRaisesRegex(RuntimeError, 'notice'):
            self.stage()
        copyright.write_bytes(b'unverified notice')
        with self.assertRaisesRegex(RuntimeError, 'notice'):
            self.stage()

    def test_wrong_source_version_notice_cannot_authorize_rootless_package(self):
        archive = self.archive()
        (self.extracted / 'usr/share/doc/libfixture1/copyright').unlink()
        with self.assertRaisesRegex(RuntimeError, 'notice'):
            self.stage(self.extracted / 'usr/lib/libfixture.so.1', self.collector([archive]))

    def test_symlinked_library_and_shared_source_notice_are_verified(self):
        self.install('fixture-common', '1.2-3', 'fixture (1.2-3)', {
            'usr/share/doc/fixture-common/copyright': self.notice})
        notice = self.system / 'usr/share/doc/libfixture1/copyright'
        notice.unlink()
        notice.symlink_to('../fixture-common/copyright')
        link = self.system / 'usr/lib/libfixture.so'
        link.symlink_to('libfixture.so.1')
        self.assertEqual(self.stage(link)['components'][0]['sourceVersion'], '1.2-3')

    def test_missing_referenced_common_license_is_rejected(self):
        (self.system / 'usr/share/common-licenses/LGPL-3').unlink()
        with self.assertRaisesRegex(RuntimeError, 'common license'):
            self.stage()

    def test_modified_common_license_is_rejected(self):
        (self.system / 'usr/share/common-licenses/LGPL-3').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'common license'):
            self.stage()

    def test_bundle_copy_must_still_match_verified_source(self):
        target = self.bundle / 'usr/lib/libfixture.so.1'
        target.parent.mkdir(parents=True)
        target.write_bytes(b'corrupted staged copy')
        with self.assertRaisesRegex(RuntimeError, 'staged'):
            self.collector().stage({'usr/lib/libfixture.so.1': self.system / 'usr/lib/libfixture.so.1'}, self.bundle)

    def test_unsafe_destination_is_rejected(self):
        for destination in ('../escape', '/absolute', 'usr/../escape', 'usr//lib/file', 'usr\\lib\\file'):
            with self.subTest(destination=destination), self.assertRaisesRegex(RuntimeError, 'destination'):
                self.collector().stage({destination: self.system / 'usr/lib/libfixture.so.1'}, self.bundle)

    def test_unrecorded_runtime_file_is_rejected(self):
        extra = self.bundle / 'usr/plugins/extra.so'
        extra.parent.mkdir(parents=True)
        extra.write_bytes(b'unrecorded')
        with self.assertRaisesRegex(RuntimeError, 'unrecorded'):
            self.stage()


if __name__ == '__main__':
    unittest.main()
