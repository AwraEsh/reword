"""Detach only known private test mounts, including disconnected FUSE mounts."""
from pathlib import Path
import re
import shutil
import subprocess


def decode_mountpoint(value):
    return re.sub(r'\\(040|011|012|134)',
                  lambda match: chr(int(match.group(1), 8)), value)


def mount_points():
    # stat/ismount can return false for disconnected FUSE endpoints. The kernel
    # mount table remains authoritative even when those endpoints cannot be read.
    with open('/proc/self/mountinfo', encoding='utf-8', errors='surrogateescape') as stream:
        data = stream.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        raise RuntimeError('Private test mount inventory exceeds its bound.')
    result = set()
    for line in data.splitlines():
        fields = line.split()
        if len(fields) < 6:
            raise RuntimeError('Private test mount inventory is malformed.')
        result.add(decode_mountpoint(fields[4]))
    return result


def detach_test_mounts(directory):
    root = Path(directory).resolve(strict=True)
    mounted = mount_points()
    for relative in ('runtime/doc', 'runtime/gvfs'):
        mount = root / relative
        if str(mount) not in mounted:
            continue
        tool = shutil.which('fusermount3') or shutil.which('fusermount')
        if not tool:
            raise RuntimeError('Cannot detach the private test FUSE mount.')
        subprocess.run([tool, '-uz', str(mount)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
