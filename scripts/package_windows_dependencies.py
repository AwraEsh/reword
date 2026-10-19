"""Map PE import names to explicit stage/system files, without DLL loading."""
from pathlib import Path, PurePosixPath
import re

from windows_pe import inspect_pe

MAX_FILES = 100000


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def checked_root(path):
    path = Path(path)
    if linked(path) or not path.is_dir():
        raise ValueError('Dependency roots must be ordinary directories')
    return path


def stage_path(root, name):
    if not isinstance(name, str) or '\\' in name or ':' in name:
        raise ValueError('Dependency inventory paths must be relative POSIX paths')
    relative = PurePosixPath(name)
    if (not relative.parts or relative.is_absolute() or str(relative) != name or
            len(relative.parts) > 32 or '..' in relative.parts):
        raise ValueError('Dependency inventory path escapes its stage')
    path = root
    for part in relative.parts:
        path /= part
        if linked(path):
            raise ValueError('Linked dependency inputs are not allowed')
    return path


def collect_dependencies(stage, inventory, system):
    stage, system = checked_root(stage), checked_root(system)
    if not isinstance(inventory, dict) or len(inventory) > MAX_FILES:
        raise ValueError('Dependency inventory exceeds its file limit')
    binaries, by_name = {}, {}
    for name, expected in sorted(inventory.items()):
        path = stage_path(stage, name)
        if path.suffix.lower() not in {'.dll', '.exe'}:
            continue
        if (not isinstance(expected, dict) or type(expected.get('bytes')) is not int or
                expected['bytes'] < 0 or not isinstance(expected.get('sha256'), str) or
                not re.fullmatch(r'[0-9a-f]{64}', expected['sha256'])):
            raise ValueError('Invalid saved PE byte identity')
        parsed = inspect_pe(path)
        if any(parsed[key] != expected[key] for key in ('bytes', 'sha256')):
            raise ValueError('A staged PE changed after its inventory')
        binaries[name] = parsed
        by_name.setdefault(path.name.lower(), []).append(name)
    if not binaries:
        raise ValueError('Dependency inventory contains no Windows binaries')

    system_names = {}
    for index, path in enumerate(system.iterdir()):
        if index >= MAX_FILES:
            raise ValueError('System directory exceeds its file limit')
        if path.suffix.lower() == '.dll':
            if linked(path) or not path.is_file():
                raise ValueError('System DLL candidates must be ordinary files')
            system_names.setdefault(path.name.lower(), []).append(path)

    names = {name for binary in binaries.values()
             for key in ('imports', 'delayImports') for name in binary[key]}
    dependencies, missing, ambiguous, contracts = {}, [], [], []
    for name in sorted(names):
        if name.startswith(('api-ms-win-', 'ext-ms-win-')):
            contracts.append(name)
            dependencies[name] = {'kind': 'api-set-contract'}
            continue
        staged = by_name.get(name, [])
        if staged:
            candidates = sorted(staged)
            identities = {(binaries[item]['bytes'], binaries[item]['sha256']) for item in candidates}
            kind = 'stage'
        elif name in system_names:
            paths = sorted(system_names[name])
            candidates = [path.name for path in paths]
            identities = set()
            for path in paths:
                parsed = inspect_pe(path)
                identities.add((parsed['bytes'], parsed['sha256']))
            kind = 'system'
        else:
            missing.append(name)
            dependencies[name] = {'kind': 'missing'}
            continue
        if len(identities) != 1:
            ambiguous.append(name)
            dependencies[name] = {'kind': 'ambiguous', 'candidates': candidates}
        else:
            size, digest = next(iter(identities))
            dependencies[name] = {'kind': kind, 'candidates': candidates,
                                  'bytes': size, 'sha256': digest}
    return {'schemaVersion': 1, 'scope': 'ordinary-and-delay-import-candidates',
            'releaseReady': False, 'ordinaryClosureComplete': not missing and not ambiguous,
            'apiSetHostCoverage': 'not-established', 'dynamicLoadingCoverage': 'not-established',
            'targetOsCoverage': 'current-system-directory-only',
            'binaries': binaries, 'dependencies': dependencies,
            'missingImports': missing, 'ambiguousImports': ambiguous, 'apiSetContracts': contracts}
