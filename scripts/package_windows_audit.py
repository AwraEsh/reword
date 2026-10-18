#!/usr/bin/env python3
"""Inventory a Windows stage against explicit vendor roots, without executing it.

This is build evidence, not source authentication or redistribution approval.
The caller owns the stage/vendor trees and must keep them unchanged while auditing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat

MAX_FILES = 100000
MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_NOTICE_BYTES = 4 * 1024 * 1024
MAX_NOTICES_BYTES = 32 * 1024 * 1024


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def traversal_error(error):
    raise error


def files(root):
    if linked(root) or not root.is_dir():
        raise ValueError('Audit roots must be ordinary directories')
    count = 0
    for parent, dirs, names in os.walk(root, followlinks=False, onerror=traversal_error):
        for name in dirs:
            if linked(Path(parent) / name):
                raise ValueError('Linked directories are not audit inputs')
        for name in names:
            path = Path(parent) / name
            if linked(path) or not stat.S_ISREG(path.stat().st_mode):
                raise ValueError('Audit inputs must be ordinary files')
            count += 1
            if count > MAX_FILES:
                raise ValueError('Audit file count exceeds the limit')
            yield path


def inventory(path, limit=MAX_FILE_BYTES):
    if linked(path) or not path.is_file():
        raise ValueError('Audit input must be an ordinary file')
    size = 0
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            size += len(block)
            if size > limit:
                raise ValueError('Audit input exceeds its byte limit')
            digest.update(block)
    return {'bytes': size, 'sha256': digest.hexdigest()}


def notice(path):
    name = path.name.lower()
    return (any(part.lower() in {'licenses', 'licences'} for part in path.parts) or
            name.startswith(('license', 'licence', 'copying', 'copyright')))


def metadata(path):
    name = path.name.lower()
    return (name in {'qconfig.pri', 'vcpkg.spdx.json', 'vcpkg.json', 'portfile.cmake',
                     'vcpkg_abi_info.txt', 'control', 'status'} or
            (name.startswith('qt_lib_') and name.endswith('.pri')))


def audit(stage, application, vendors, output):
    stage, application, output = map(Path, (stage, application, output))
    if output.exists() or output.is_symlink():
        raise ValueError('Audit output already exists')
    if not vendors or any(not re.fullmatch(r'[a-z][a-z0-9-]{0,31}', key) for key in vendors):
        raise ValueError('Explicit safe vendor identifiers are required')
    staged = {path.relative_to(stage).as_posix(): inventory(path) for path in files(stage)}
    app = inventory(application)
    binaries = {name: {**item, 'matches': []} for name, item in staged.items()
                if Path(name).suffix.lower() in {'.exe', '.dll'}}
    if not binaries:
        raise ValueError('Stage contains no Windows binaries')
    by_name = {}
    for name in binaries:
        by_name.setdefault(Path(name).name.lower(), []).append(name)
    if 'reword.exe' in binaries and all(binaries['reword.exe'][k] == app[k] for k in app):
        binaries['reword.exe']['matches'].append('application')
    notices, build_metadata, notice_inputs = {}, {}, {}
    missing, notice_bytes = [], 0
    for vendor, location in sorted(vendors.items()):
        root = Path(location)
        has_notice = False
        for path in sorted(files(root)):
            relative = path.relative_to(root)
            label = vendor + '/' + relative.as_posix()
            names = by_name.get(path.name.lower(), [])
            if names:
                original = inventory(path)
                for name in names:
                    if all(binaries[name][k] == original[k] for k in original):
                        binaries[name]['matches'].append(label)
            if notice(relative):
                item = inventory(path, MAX_NOTICE_BYTES)
                notice_bytes += item['bytes']
                if notice_bytes > MAX_NOTICES_BYTES:
                    raise ValueError('Vendor notice collection exceeds its byte limit')
                notices[label] = item
                notice_inputs[label] = path
                has_notice = True
            elif metadata(relative):
                build_metadata[label] = inventory(path, MAX_NOTICE_BYTES)
        if not has_notice:
            missing.append(vendor)
    for item in binaries.values():
        item['matches'].sort()
    report = {
        'schemaVersion': 1,
        'purpose': 'windows-build-materials-audit',
        'releaseReady': False,
        'sourceAuthentication': 'not-established-by-this-audit',
        'runtimeNoticeCoverage': 'not-established-by-vendor-directory-scan',
        'files': staged,
        'binaries': binaries,
        'unmatchedBinaries': sorted(name for name, item in binaries.items() if not item['matches']),
        'missingVendorNotices': missing,
        'vendorNotices': notices,
        'vendorBuildMetadata': build_metadata,
    }
    # Gather/validate before claiming an output directory. Never replace an older audit.
    output.mkdir(parents=False, exist_ok=False)
    try:
        for name, source in notice_inputs.items():
            target = output / 'notices' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open('rb') as incoming, target.open('xb') as outgoing:
                copied = 0
                while block := incoming.read(64 * 1024):
                    copied += len(block)
                    if copied > MAX_NOTICE_BYTES:
                        raise ValueError('Vendor notice grew beyond its limit')
                    outgoing.write(block)
            if inventory(target, MAX_NOTICE_BYTES) != notices[name]:
                raise ValueError('Vendor notice changed during collection')
        (output / 'audit.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    except BaseException:
        shutil.rmtree(output)
        raise
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--vendor', action='append', required=True, metavar='NAME=PATH')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    vendors = {}
    for value in args.vendor:
        name, separator, path = value.partition('=')
        if not separator or not path or name in vendors:
            parser.error('Each vendor needs a unique NAME=PATH')
        vendors[name] = Path(path)
    try:
        report = audit(args.stage, args.application, vendors, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, 'Windows materials audit failed: ' + str(error) + '\n')
    print(json.dumps({'files': len(report['files']), 'binaries': len(report['binaries']),
                      'unmatchedBinaries': report['unmatchedBinaries'],
                      'missingVendorNotices': report['missingVendorNotices']}))


if __name__ == '__main__':
    main()
