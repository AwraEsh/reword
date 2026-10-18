"""Copy the Qt scanner's import closure without unrelated nested QML modules."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from package_provenance import safe_relative


def _formal_module(path):
    qmldir = path / 'qmldir'
    match = (re.search(r'^\s*module\s+(\S+)', qmldir.read_text(), re.MULTILINE)
             if qmldir.is_file() else None)
    return match[1] if match else None


def _ignored_directories(directory, names):
    return [child for child in names if (Path(directory) / child).is_dir() and
            (child == 'designer' or _formal_module(Path(directory) / child))]


def _own_qml_imports(source):
    imports = set()
    for directory, children, files in os.walk(source, followlinks=True):
        ignored = _ignored_directories(directory, children)
        children[:] = [name for name in children if name not in ignored]
        for name in files:
            if name.endswith('.qml'):
                pattern = r'^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)'
            elif name.endswith('.js'):
                pattern = r'^\s*\.import\s+([A-Za-z_][A-Za-z0-9_.]*)'
            else:
                continue
            imports.update(re.findall(pattern, (Path(directory) / name).read_text(), re.MULTILINE))
    return imports


def deploy_qml(root, import_root, qt_prefix, destination, copy_function):
    root, import_root, qt_prefix = Path(root), Path(import_root), Path(qt_prefix)
    source_root = root / 'qml'
    if not source_root.is_dir():
        return []
    override = os.environ.get('REWORD_QMLIMPORTSCANNER')
    candidates = [Path(override)] if override else [
        qt_prefix / 'lib/qt6/libexec/qmlimportscanner',
        qt_prefix / 'lib/qt6/bin/qmlimportscanner',
        qt_prefix / 'libexec/qmlimportscanner', qt_prefix / 'bin/qmlimportscanner']
    scanner = next((p for p in candidates if p.is_file()), None)
    if not scanner:
        raise RuntimeError('qmlimportscanner is required to package the QML runtime.')
    result = subprocess.run([str(scanner.resolve()), '-rootPath', str(source_root.resolve()),
                             '-importPath', str(import_root.resolve())],
                            cwd=root, check=True, capture_output=True)
    try:
        entries = json.loads(result.stdout)
    except (ValueError, UnicodeError) as error:
        raise RuntimeError('Invalid QML dependency scan output.') from error
    if not isinstance(entries, list):
        raise RuntimeError('Invalid QML dependency scan output.')
    direct = set()
    for path in source_root.rglob('*.qml'):
        direct.update(re.findall(r'^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)',
                                 path.read_text(), re.MULTILINE))
    modules, unresolved, optional = {}, set(), set()
    required = set(direct)
    for entry in entries:
        if not isinstance(entry, dict):
            raise RuntimeError('Invalid QML dependency scan entry.')
        if entry.get('type') != 'module':
            continue  # Project-local files/directories are already embedded in the app.
        name = entry.get('name')
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.]*', name):
            raise RuntimeError('Invalid QML module identity.')
        if not entry.get('path'):
            unresolved.add(name)
            continue
        relative, source = entry.get('relativePath'), entry['path']
        if not isinstance(relative, str) or not safe_relative(relative) or not isinstance(source, str):
            raise RuntimeError('Unsafe QML module path.')
        source = Path(source)
        if not source.is_absolute():
            source = root / source
        expected = import_root / relative
        if source.resolve() != expected.resolve() or _formal_module(source) != name:
            raise RuntimeError('QML module path does not match its import root.')
        value = (relative, source)
        if name in modules and modules[name] != value:
            raise RuntimeError('Conflicting QML module paths.')
        modules[name] = value
        qmldir = (source / 'qmldir').read_text()
        optional.update(re.findall(r'^\s*optional\s+import\s+(\S+)', qmldir, re.MULTILINE))
        required.update(re.findall(r'^\s*(?:default\s+)?(?:import|depends)\s+(\S+)',
                                   qmldir, re.MULTILINE))
        required.update(_own_qml_imports(source))
    missing = (unresolved - optional) | (required - modules.keys())
    if missing:
        raise RuntimeError('Unresolved QML imports: ' + ', '.join(sorted(missing)))
    seen = {}
    for name, (relative, source) in sorted(modules.items()):
        if relative in seen and seen[relative] != name:
            raise RuntimeError('Conflicting QML module destinations.')
        seen[relative] = name

        shutil.copytree(source, Path(destination) / relative, symlinks=False,
                        dirs_exist_ok=True, copy_function=copy_function, ignore=_ignored_directories)
    return sorted(modules)
