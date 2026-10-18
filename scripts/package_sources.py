#!/usr/bin/env python3
"""Assemble exact Debian source packages offline; never extract source archives.

Descriptor provenance must be established by the acquisition process. This tool
checks identity and archive integrity, not OpenPGP signature authenticity.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
from urllib.parse import quote

_NAME = re.compile(r'[a-z0-9][a-z0-9+.-]+\Z')
_VERSION = re.compile(r'(?:[0-9]+:)?[0-9][A-Za-z0-9.+~\-]*\Z')
_FILE = re.compile(r'[A-Za-z0-9][A-Za-z0-9.+~_-]*\Z')
_MAX_DESCRIPTOR = 2 * 1024 * 1024
_MAX_MANIFEST = 16 * 1024 * 1024


def regular(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f'Not a regular source file: {path.name}')


def open_directory(path):
    if not hasattr(os, 'O_NOFOLLOW'):
        raise ValueError('Source assembly requires Linux no-follow file access')
    path = path.absolute()
    if '..' in path.parts:
        raise ValueError('Parent traversal in source path')
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    descriptor = os.open(path.anchor, flags)
    try:
        for part in path.parts[1:]:
            child = os.open(part, flags, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def open_regular(path):
    # Readable preflight error, followed by protection on the actual open.
    regular(path)
    parent = open_directory(path.parent)
    try:
        descriptor = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                             dir_fd=parent)
    finally:
        os.close(parent)
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError(f'Not a regular opened source file: {path.name}')
        return os.fdopen(descriptor, 'rb')
    except BaseException:
        os.close(descriptor)
        raise


def read_small(path, limit):
    with open_regular(path) as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f'Metadata too large: {path.name}')
    return data


def publish_no_replace(staging, output):
    # Ordinary rename can replace an empty directory created concurrently.
    libc = ctypes.CDLL(None, use_errno=True)
    function = getattr(libc, 'renameat2', None)
    if function is None:
        raise OSError(errno.ENOSYS, 'Atomic no-replace publication is unavailable')
    function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    function.restype = ctypes.c_int
    source_parent = open_directory(staging.parent)
    try:
        target_parent = open_directory(output.parent)
        try:
            result = function(source_parent, os.fsencode(staging.name), target_parent,
                              os.fsencode(output.name), 1)  # RENAME_NOREPLACE
            if result:
                code = ctypes.get_errno()
                raise OSError(code, os.strerror(code), output.name)
        finally:
            os.close(target_parent)
    finally:
        os.close(source_parent)


def control_fields(data):
    text = data.decode('utf-8')
    lines = text.splitlines()
    if lines and lines[0] == '-----BEGIN PGP SIGNED MESSAGE-----':
        try:
            start = lines.index('') + 1
            signature = lines.index('-----BEGIN PGP SIGNATURE-----', start)
        except ValueError as error:
            raise ValueError('Incomplete descriptor signature armor') from error
        if (not all(re.fullmatch(r'Hash: [A-Za-z0-9, -]+', x) for x in lines[1:start-1])
                or not lines[1:start-1]
                or not lines[signature+1:]
                or lines[-1] != '-----END PGP SIGNATURE-----'):
            raise ValueError('Malformed descriptor signature armor')
        lines = [line[2:] if line.startswith('- ') else line for line in lines[start:signature]]
    fields = {}
    key = None
    ended = False
    for line in lines:
        if not line:
            if fields:
                ended = True
            continue
        if ended:
            raise ValueError('Multiple descriptor control paragraphs')
        if line[0] in ' \t':
            if key is None:
                raise ValueError('Orphan descriptor continuation')
            fields[key] += '\n' + line[1:]
            continue
        match = re.fullmatch(r'([A-Za-z0-9-]+):[ \t]*(.*)', line)
        if not match:
            raise ValueError('Malformed descriptor field')
        key = match[1].lower()
        if key in fields:
            raise ValueError(f'Duplicate descriptor field: {key}')
        fields[key] = match[2]
    return fields


def identity(name, version):
    if not isinstance(name, str) or not _NAME.fullmatch(name):
        raise ValueError('Invalid source package name')
    if not isinstance(version, str) or not _VERSION.fullmatch(version) or len(version) > 200:
        raise ValueError('Invalid source package version')
    return name, version


def checksums(fields):
    inventories = {}
    for field, algorithm, length in [('checksums-sha256', 'sha256', 64),
                                     ('checksums-sha1', 'sha1', 40), ('files', 'md5', 32)]:
        if field not in fields:
            if algorithm == 'sha256':
                raise ValueError('Missing Checksums-Sha256')
            continue
        entries = {}
        for line in fields[field].splitlines():
            if not line.strip():
                continue
            parts = line.split()
            if len(parts) != 3:
                raise ValueError('Malformed source checksum entry')
            digest, size, filename = parts
            if (not re.fullmatch('[0-9a-fA-F]{' + str(length) + '}', digest)
                    or not size.isascii() or not size.isdigit()
                    or not _FILE.fullmatch(filename) or filename.endswith('.dsc')
                    or filename in entries):
                raise ValueError('Unsafe or duplicate source archive entry')
            count = int(size)
            if count <= 0 or count > 16 * 1024**3:
                raise ValueError('Invalid source archive size')
            entries[filename] = (count, digest.lower())
        if not entries or len(entries) > 256:
            raise ValueError('Invalid source archive count')
        inventories[algorithm] = entries
    expected = {name: item[0] for name, item in inventories['sha256'].items()}
    for entries in inventories.values():
        if {name: item[0] for name, item in entries.items()} != expected:
            raise ValueError('Source checksum inventories disagree')
    return inventories


def descriptors(source_root):
    if source_root.is_symlink() or not source_root.is_dir():
        raise ValueError('Source input must be a real directory')
    count = 0
    for base, directories, files in os.walk(source_root, followlinks=False):
        for directory in directories:
            if (Path(base) / directory).is_symlink():
                raise ValueError('Symlinked source directory')
        for filename in sorted(files):
            if not filename.endswith('.dsc'):
                continue
            count += 1
            if count > 2048:
                raise ValueError('Too many source descriptors')
            if not _FILE.fullmatch(filename):
                raise ValueError('Unsafe source descriptor filename')
            path = Path(base) / filename
            data = read_small(path, _MAX_DESCRIPTOR)
            fields = control_fields(data)
            key = identity(fields.get('source'), fields.get('version'))
            yield key, path, data, checksums(fields)


def copy_archive(source, target, size, expected):
    hashes = {algorithm: hashlib.new(algorithm) for algorithm in expected}
    total = 0
    with open_regular(source) as reader, target.open('xb') as writer:
        if os.fstat(reader.fileno()).st_size != size:
            raise ValueError(f'Source archive size mismatch: {source.name}')
        while True:
            block = reader.read(min(1024 * 1024, size - total + 1))
            if not block:
                break
            total += len(block)
            if total > size:
                raise ValueError(f'Source archive grew: {source.name}')
            for digest in hashes.values():
                digest.update(block)
            writer.write(block)
    if total != size or any(hashes[a].hexdigest() != h for a, h in expected.items()):
        raise ValueError(f'Source archive checksum mismatch: {source.name}')
    return hashes['sha256'].hexdigest()


def assemble(manifest, source_root, output, trust_config=None):
    if output.exists() or output.is_symlink():
        raise ValueError('Source output already exists')
    manifest_data = read_small(manifest, _MAX_MANIFEST)
    inventory = json.loads(manifest_data)
    third_party = inventory['thirdParty']
    if third_party['schemaVersion'] != 1 or not isinstance(third_party['components'], list):
        raise ValueError('Unsupported runtime provenance schema')
    components = third_party['components']
    if not components or len(components) > 4096:
        raise ValueError('Invalid runtime component count')
    required = {identity(c['sourcePackage'], c['sourceVersion']) for c in components}
    selected = {}
    for key, path, data, archives in descriptors(source_root):
        if key in required:
            if key in selected:
                raise ValueError(f'Ambiguous source descriptor: {key[0]} {key[1]}')
            selected[key] = path, data, archives
    missing = required - selected.keys()
    if missing:
        raise ValueError('Missing exact source package: ' + ', '.join(n + ' ' + v for n, v in sorted(missing)))
    authentication = None
    if trust_config is not None:
        from package_source_auth import authenticate
        authentication = authenticate(trust_config, {key: (path.name, data)
            for key, (path, data, _) in selected.items()})
    total = sum(len(data) + sum(x[0] for x in archives['sha256'].values())
                for _, data, archives in selected.values())
    if total > 64 * 1024**3:
        raise ValueError('Source bundle exceeds 64 GiB')
    if shutil.disk_usage(output.parent).free < total + 64 * 1024**2:
        raise ValueError('Insufficient disk space for complete source bundle')
    staging = Path(tempfile.mkdtemp(prefix='.' + output.name + '-', dir=output.parent))
    try:
        index = {'schemaVersion': 1, 'runtimeManifestSha256': hashlib.sha256(manifest_data).hexdigest(),
                 'descriptorAuthentication': 'verified' if authentication else 'not-verified', 'sources': []}
        if authentication:
            index['authentication'] = authentication
        for (name, version), (descriptor, data, inventories) in sorted(selected.items()):
            directory = staging / name / quote(version, safe='+~.-')
            directory.mkdir(parents=True)
            descriptor_target = directory / descriptor.name
            descriptor_target.write_bytes(data)
            entry = {'package': name, 'version': version,
                     'descriptor': {'path': descriptor_target.relative_to(staging).as_posix(),
                                    'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
                     'archives': []}
            for filename, (size, _) in sorted(inventories['sha256'].items()):
                target = directory / filename
                expected = {a: items[filename][1] for a, items in inventories.items()}
                digest = copy_archive(descriptor.parent / filename, target, size, expected)
                entry['archives'].append({'path': target.relative_to(staging).as_posix(),
                                          'size': size, 'sha256': digest})
            index['sources'].append(entry)
        (staging / 'source-index.json').write_text(json.dumps(index, indent=2) + '\n', encoding='utf-8')
        publish_no_replace(staging, output)
        return index
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--trust-config', type=Path, help='Require authenticated descriptors under explicit keyrings/indexes')
    args = parser.parse_args()
    try:
        index = assemble(args.manifest, args.input, args.output, args.trust_config)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Source bundle rejected: {error}\n')
    print(f"Verified {len(index['sources'])} exact source packages")


if __name__ == '__main__':
    main()
