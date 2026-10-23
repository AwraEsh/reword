"""Only known mountpoints beneath the caller-owned test directory may detach."""
from pathlib import Path
import io
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from owned_mounts import detach_test_mounts, decode_mountpoint, mount_points


class OwnedMountTest(unittest.TestCase):
    def test_mount_inventory_reads_kernel_mountpoint_field(self):
        data = '31 22 0:9 / /tmp/test\\040space/runtime/doc rw - fuse.portal portal rw\n'
        with patch('builtins.open', return_value=io.StringIO(data)):
            self.assertEqual(mount_points(), {'/tmp/test space/runtime/doc'})

    def test_malformed_or_oversized_inventory_fails_closed(self):
        for data in ('broken\n', 'x' * (1024 * 1024 + 1)):
            with patch('builtins.open', return_value=io.StringIO(data)):
                with self.assertRaises(RuntimeError):
                    mount_points()

    def test_mountinfo_escaping_is_decoded_once(self):
        self.assertEqual(decode_mountpoint(r'/tmp/a\040b\134040'), '/tmp/a b\\040')

    def test_disconnected_mount_is_detached_even_when_stat_cannot_recognize_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            with patch('owned_mounts.mount_points', return_value={str(root / 'runtime/doc')}), \
                 patch('owned_mounts.shutil.which', return_value='/usr/bin/fusermount3'), \
                 patch('owned_mounts.subprocess.run') as run:
                detach_test_mounts(directory)
                self.assertEqual(run.call_count, 1)
                self.assertEqual(run.call_args.args[0],
                                 ['/usr/bin/fusermount3', '-uz', str(root / 'runtime/doc')])

    def test_foreign_and_unexpected_mounts_are_never_detached(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            with patch('owned_mounts.mount_points', return_value={
                    '/tmp/other/runtime/doc', str(root / 'unrelated')}), \
                 patch('owned_mounts.subprocess.run') as run:
                detach_test_mounts(directory)
                run.assert_not_called()

    def test_missing_unmount_tool_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            with patch('owned_mounts.mount_points', return_value={str(root / 'runtime/doc')}), \
                 patch('owned_mounts.shutil.which', return_value=None):
                with self.assertRaises(RuntimeError):
                    detach_test_mounts(directory)


if __name__ == '__main__':
    unittest.main()
