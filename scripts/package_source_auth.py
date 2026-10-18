"""Offline authentication of exact Debian source descriptors, never key discovery."""
import hashlib
import json
import lzma
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile

from package_sources import control_fields, identity, read_small

_MAX_META = 2 * 1024**2
_MAX_XZ = 64 * 1024**2
_MAX_EXPANDED = 512 * 1024**2


def _local(base, value):
    if not isinstance(value, str) or not value or '\x00' in value:
        raise ValueError('Invalid authentication input path')
    path = Path(value)
    return path if path.is_absolute() else base / path


def _verify(data, keyrings, home):
    if not keyrings or not data.startswith(b'-----BEGIN PGP SIGNED MESSAGE-----'):
        raise ValueError('No trusted cleartext signature')
    command = ['gpgv', '--homedir', str(home), '--status-fd', '2', '--output', '-']
    for keyring in keyrings:
        command += ['--keyring', str(keyring)]
    try:
        result = subprocess.run(command + ['-'], input=data, capture_output=True, timeout=15)
    except subprocess.TimeoutExpired as error:
        raise ValueError('Source signature verification timed out') from error
    # Display UIDs can have legacy encodings. Only ASCII machine status counts.
    status = result.stderr.decode('utf-8', errors='replace').splitlines()
    signers = []
    invalid = {'BADSIG', 'ERRSIG', 'NO_PUBKEY', 'EXPSIG', 'EXPKEYSIG', 'REVKEYSIG'}
    failed = False
    for line in status:
        parts = line.split()
        if len(parts) < 2 or parts[0] != '[GNUPG:]':
            continue
        if parts[1] in invalid:
            failed = True
        if parts[1] == 'VALIDSIG' and len(parts) > 2 and re.fullmatch(r'[A-F0-9]{40,64}', parts[2]):
            signers.append(parts[2])
    if result.returncode or failed or not signers or len(result.stdout) > _MAX_META:
        raise ValueError('Source signature is not valid under supplied trust roots')
    return result.stdout, sorted(set(signers))


def _sha_entries(value):
    entries = {}
    for line in value.splitlines():
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 3:
            raise ValueError('Malformed SHA256 inventory')
        digest, count, name = parts
        if (not re.fullmatch(r'[a-fA-F0-9]{64}', digest) or not count.isascii()
                or not count.isdigit() or name in entries):
            raise ValueError('Invalid or duplicate SHA256 entry')
        entries[name] = int(count), digest.lower()
    return entries


def _expanded(compressed, expected):
    size, digest = expected
    if size < 0 or size > _MAX_EXPANDED:
        raise ValueError('Expanded source index exceeds bound')
    decoder = lzma.LZMADecompressor(memlimit=64 * 1024**2)
    checksum = hashlib.sha256()
    total = 0
    for start in range(0, len(compressed), 1024**2):
        block = compressed[start:start + 1024**2]
        while True:
            data = decoder.decompress(block, max_length=1024**2)
            block = b''
            total += len(data)
            if total > size:
                raise ValueError('Expanded source index grew beyond signed size')
            checksum.update(data)
            if data:
                yield data
            if decoder.eof:
                if decoder.unused_data or start + 1024**2 < len(compressed):
                    raise ValueError('Trailing data after source index stream')
                break
            if decoder.needs_input:
                break
        if decoder.eof:
            break
    if not decoder.eof or total != size or checksum.hexdigest() != digest:
        raise ValueError('Expanded source index checksum mismatch')


def _paragraphs(chunks):
    pending = b''
    for chunk in chunks:
        pending += chunk
        while b'\n\n' in pending:
            row, pending = pending.split(b'\n\n', 1)
            if len(row) > _MAX_META:
                raise ValueError('Source index paragraph exceeds bound')
            if row:
                yield control_fields(row)
        if len(pending) > _MAX_META:
            raise ValueError('Source index paragraph exceeds bound')
    if pending.strip():
        yield control_fields(pending)


def _load_index(item, config_dir, keyrings, home, required):
    path = item['path']
    if (not isinstance(path, str) or '\\' in path or '\x00' in path
            or PurePosixPath(path).is_absolute() or '..' in PurePosixPath(path).parts
            or str(PurePosixPath(path)) != path or not path.endswith('/Sources.xz')):
        raise ValueError('Unsafe archive source index path')
    release = read_small(_local(config_dir, item['release']), _MAX_META)
    payload, signers = _verify(release, keyrings, home)
    fields = control_fields(payload)
    if fields.get('origin') != 'Debian':
        raise ValueError('Source index is not from Debian')
    expected = _sha_entries(fields['sha256'])
    compressed = read_small(_local(config_dir, item['sources']), _MAX_XZ)
    if (len(compressed), hashlib.sha256(compressed).hexdigest()) != expected[path]:
        raise ValueError('Compressed source index checksum mismatch')
    entries = {}
    for row in _paragraphs(_expanded(compressed, expected[path[:-3]])):
        key = row.get('package'), row.get('version')
        if key not in required:
            continue
        if key in entries:
            raise ValueError('Duplicate source identity in authenticated index')
        candidates = [(name, size, digest) for name, (size, digest) in
                      _sha_entries(row['checksums-sha256']).items() if name.endswith('.dsc')]
        if len(candidates) != 1:
            raise ValueError('Ambiguous descriptor in authenticated source index')
        entries[key] = candidates[0]
    evidence = {'inReleaseSha256': hashlib.sha256(release).hexdigest(), 'sourceIndexPath': path,
                'sourcesXzSha256': hashlib.sha256(compressed).hexdigest(),
                'sourcesSha256': expected[path[:-3]][1], 'signers': signers,
                'origin': fields['origin'], 'codename': fields.get('codename', ''),
                'date': fields.get('date', ''), 'validUntil': fields.get('valid-until', '')}
    return entries, evidence


def authenticate(config, descriptors):
    """Verify fixed identities; config paths are explicit trust roots, not discovered keys."""
    settings = json.loads(read_small(config, 128 * 1024))
    if settings['schemaVersion'] != 1 or not descriptors or len(descriptors) > 2048:
        raise ValueError('Invalid source authentication request')
    if not isinstance(settings.get('indexes', []), list) or len(settings.get('indexes', [])) > 8:
        raise ValueError('Invalid source authentication index count')
    required = {identity(*key) for key in descriptors}
    with tempfile.TemporaryDirectory(prefix='reword-source-auth-') as temporary:
        home = Path(temporary)
        evidence = {'schemaVersion': 1, 'purpose': 'exact-source-identity',
                    'keyrings': [], 'indexes': [], 'sources': []}
        roots = {}
        for role, setting in [('maintainer', 'maintainerKeyrings'), ('archive', 'archiveKeyrings')]:
            paths = settings.get(setting, [])
            if not isinstance(paths, list) or len(paths) > 4:
                raise ValueError('Invalid authentication keyring count')
            roots[role] = []
            for number, value in enumerate(paths):
                data = read_small(_local(config.parent, value), 64 * 1024**2)
                target = home / f'{role}-{number}.gpg'
                target.write_bytes(data)
                roots[role].append(target)
                evidence['keyrings'].append({'role': role, 'sha256': hashlib.sha256(data).hexdigest()})
        indexed = {}
        for item in settings.get('indexes', []):
            try:
                rows, proof = _load_index(item, config.parent, roots['archive'], home, required)
            except lzma.LZMAError as error:
                raise ValueError('Invalid compressed source index') from error
            index_id = proof['inReleaseSha256']
            evidence['indexes'].append(proof)
            for key, value in rows.items():
                if key in indexed and indexed[key][0] != value:
                    raise ValueError('Conflicting authenticated source indexes')
                indexed[key] = value, index_id
        for key in sorted(required):
            filename, data = descriptors[key]
            if len(data) > _MAX_META:
                raise ValueError('Source descriptor exceeds bound')
            fields = control_fields(data)
            if identity(fields.get('source'), fields.get('version')) != key:
                raise ValueError('Descriptor does not match requested source identity')
            digest = hashlib.sha256(data).hexdigest()
            proof = {'package': key[0], 'version': key[1], 'descriptorSha256': digest}
            if key in indexed:
                expected, index_id = indexed[key]
                if (filename, len(data), digest) != expected:
                    raise ValueError('Descriptor differs from authenticated source index')
                proof.update({'method': 'archive-index', 'inReleaseSha256': index_id})
            else:
                try:
                    _, signers = _verify(data, roots['maintainer'], home)
                except ValueError as error:
                    raise ValueError(f'Cannot authenticate source: {key[0]} {key[1]}') from error
                proof.update({'method': 'maintainer-signature', 'signers': signers})
            evidence['sources'].append(proof)
        return evidence
