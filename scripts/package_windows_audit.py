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
MAX_BUILD_METADATA_BYTES = 1024 * 1024
MAX_BUILD_METADATA_TOTAL_BYTES = 8 * 1024 * 1024


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


def audit(stage, application, vendors, output, system=None):
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
    notices, build_metadata, notice_inputs, metadata_inputs = {}, {}, {}, {}
    missing, notice_bytes, metadata_bytes = [], 0, 0
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
                item = inventory(path, MAX_BUILD_METADATA_BYTES)
                metadata_bytes += item['bytes']
                if metadata_bytes > MAX_BUILD_METADATA_TOTAL_BYTES:
                    raise ValueError('Vendor build metadata collection exceeds its byte limit')
                build_metadata[label] = item
                metadata_inputs[label] = path
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
    if system is not None:
        from package_windows_dependencies import collect_dependencies
        try:
            report['dependencyClosure'] = collect_dependencies(stage, staged, system)
        except (OSError, ValueError):
            # Preserve the trusted materials inventory even if PE collection fails.
            # A fixed classification avoids retaining paths or exception contents.
            report['dependencyClosure'] = {
                'schemaVersion': 1,
                'scope': 'ordinary-and-delay-import-candidates',
                'releaseReady': False,
                'ordinaryClosureComplete': False,
                'collectionComplete': False,
                'failure': 'invalid-or-unavailable-pe-metadata',
            }
    # Gather/validate before claiming an output directory. Never replace an older audit.
    output.mkdir(parents=False, exist_ok=False)
    try:
        collections = (
            ('notices', notice_inputs, notices, MAX_NOTICE_BYTES),
            ('build-metadata', metadata_inputs, build_metadata, MAX_BUILD_METADATA_BYTES),
        )
        for directory, inputs, recorded, limit in collections:
            for name, source in inputs.items():
                target = output / directory / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open('rb') as incoming, target.open('xb') as outgoing:
                    copied = 0
                    while block := incoming.read(64 * 1024):
                        copied += len(block)
                        if copied > limit:
                            raise ValueError('Vendor material grew beyond its limit')
                        outgoing.write(block)
                if inventory(target, limit) != recorded[name]:
                    raise ValueError('Vendor material changed during collection')
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
    parser.add_argument('--system-directory', type=Path,
                        help='Explicit current Windows system directory for strict ordinary DLL checks')
    args = parser.parse_args()
    vendors = {}
    for value in args.vendor:
        name, separator, path = value.partition('=')
        if not separator or not path or name in vendors:
            parser.error('Each vendor needs a unique NAME=PATH')
        vendors[name] = Path(path)
    try:
        report = audit(args.stage, args.application, vendors, args.output, system=args.system_directory)
    except (OSError, ValueError) as error:
        parser.exit(1, 'Windows materials audit failed: ' + str(error) + '\n')
    print(json.dumps({'files': len(report['files']), 'binaries': len(report['binaries']),
                      'unmatchedBinaries': report['unmatchedBinaries'],
                      'missingVendorNotices': report['missingVendorNotices']}))
    closure = report.get('dependencyClosure')
    if closure is not None and not closure['ordinaryClosureComplete']:
        parser.exit(1, 'Windows DLL dependency audit failed; inspect the retained audit.json.\n')


if __name__ == '__main__':
    main()
