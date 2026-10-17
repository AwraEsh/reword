"""Verify Debian runtime origins and stage exact notices. Build-time only; no network."""
from dataclasses import dataclass
from email.parser import Parser
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile


def fingerprint(path):
    md5, sha256 = hashlib.md5(), hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            md5.update(block)
            sha256.update(block)
    return md5.hexdigest(), sha256.hexdigest()


def parse_sums(text):
    sums = {}
    for line in text.splitlines():
        digest, separator, name = line.partition('  ')
        if not separator or not re.fullmatch('[0-9a-f]{32}', digest):
            raise RuntimeError('Invalid package checksum metadata.')
        if not safe_relative(name) or name in sums:
            raise RuntimeError('Unsafe or duplicate package checksum path.')
        sums[name] = digest
    return sums


def safe_relative(name):
    return (bool(name) and '\\' not in name and '\0' not in name and
            not PurePosixPath(name).is_absolute() and
            all(part not in ('.', '..') for part in name.split('/')) and
            PurePosixPath(name).as_posix() == name)


@dataclass
class Package:
    name: str
    architecture: str
    version: str
    source: str
    source_version: str
    sums: dict | None = None
    archive_sha256: str | None = None

    @property
    def identifier(self):
        return self.name + ':' + self.architecture

    @property
    def key(self):
        return self.identifier + '=' + self.version

    def validate(self):
        for name in (self.name, self.source):
            if not re.fullmatch('[a-z0-9][a-z0-9+.-]*', name):
                raise RuntimeError('Invalid package identity metadata.')
        if not re.fullmatch('[a-z0-9][a-z0-9-]*', self.architecture):
            raise RuntimeError('Invalid package architecture metadata.')
        for version in (self.version, self.source_version):
            if not re.fullmatch(r'[0-9][A-Za-z0-9.+:~\-]*', version):
                raise RuntimeError('Invalid package version metadata.')
        return self


class DebianProvenance:
    def __init__(self, system_root=Path('/'), admin_dir=Path('/var/lib/dpkg'),
                 extracted_root=None, archives=()):
        self.system_root = Path(system_root).resolve()
        self.admin_dir = Path(admin_dir)
        self.extracted_root = Path(extracted_root).resolve() if extracted_root else None
        self.installed = {}
        self.archived = []
        self.archive_owners = {}
        self.verified = {}
        self.owners = {}
        output = self._run(['dpkg-query', '--admindir=' + str(self.admin_dir), '-W',
                            '-f=${Package}\t${Architecture}\t${Version}\t${source:Package}'
                            '\t${source:Version}\t${db:Status-Status}\n']).decode()
        for line in output.splitlines():
            fields = line.split('\t')
            if len(fields) != 6:
                raise RuntimeError('Invalid installed package metadata.')
            if fields[-1] != 'installed':
                continue
            package = Package(*fields[:5])
            self.installed[package.identifier] = package
        for archive in archives:
            self._add_archive(Path(archive))

    @staticmethod
    def _run(args):
        return subprocess.run(args, check=True, capture_output=True,
                              env=dict(os.environ, LC_ALL='C')).stdout

    def _add_archive(self, archive):
        control = Parser().parsestr(self._run(['dpkg-deb', '-f', str(archive)]).decode())
        name, version = control['Package'], control['Version']
        source = control.get('Source', name)
        match = re.fullmatch(r'([a-z0-9][a-z0-9+.-]*)(?: \(([^()]+)\))?', source)
        if not match:
            raise RuntimeError('Invalid archive source identity metadata.')
        raw = self._run(['dpkg-deb', '--ctrl-tarfile', str(archive)])
        if len(raw) > 32 * 1024 * 1024:
            raise RuntimeError('Package control metadata exceeds the build-time limit.')
        with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as control_tar:
            members = [m for m in control_tar if m.name in ('md5sums', './md5sums')]
            if len(members) != 1 or not members[0].isfile() or members[0].size > 16 * 1024 * 1024:
                raise RuntimeError('Missing package checksum metadata.')
            sums = parse_sums(control_tar.extractfile(members[0]).read().decode())
        package = Package(name, control['Architecture'], version, match[1], match[2] or version,
                          sums, fingerprint(archive)[1]).validate()
        self.archived.append(package)
        for name, digest in sums.items():
            self.archive_owners.setdefault(name, []).append((package, digest))

    def _installed_sums(self, package):
        if package.sums is None:
            candidates = [self.admin_dir / 'info' / (name + '.md5sums')
                          for name in (package.identifier, package.name)]
            path = next((path for path in candidates if path.is_file()), None)
            if not path:
                raise RuntimeError('Missing installed package checksum metadata.')
            package.sums = parse_sums(path.read_text())
        return package.sums

    def _system_owners(self, relative):
        if relative not in self.owners:
            try:
                output = self._run(['dpkg-query', '--admindir=' + str(self.admin_dir),
                                    '-S', '--', '/' + relative]).decode()
            except subprocess.CalledProcessError as error:
                raise RuntimeError('No verified package origin for a runtime file.') from error
            identifiers = []
            for line in output.splitlines():
                owners, separator, path = line.rpartition(': ')
                if separator and path == '/' + relative:
                    identifiers.extend(owners.split(', '))
            candidates = []
            for identifier in identifiers:
                if identifier in self.installed:
                    candidates.append(self.installed[identifier])
                else:
                    candidates.extend(p for p in self.installed.values() if p.name == identifier)
            self.owners[relative] = candidates
        return self.owners[relative]

    def _resolve(self, path):
        path = Path(path).resolve(strict=True)
        if path in self.verified:
            return self.verified[path]
        md5, sha256 = fingerprint(path)
        if self.extracted_root and path.is_relative_to(self.extracted_root):
            relative = path.relative_to(self.extracted_root).as_posix()
            candidates = self.archive_owners.get(relative, [])
        elif path.is_relative_to(self.system_root):
            relative = path.relative_to(self.system_root).as_posix()
            candidates = [(p, self._installed_sums(p).get(relative))
                          for p in self._system_owners(relative)]
        else:
            raise RuntimeError('No verified package origin for a runtime file.')
        if not candidates:
            raise RuntimeError('No verified package origin for a runtime file.')
        matches = {p.key: p for p, digest in candidates if digest == md5}
        if len(matches) != 1:
            raise RuntimeError('Runtime package checksum mismatch or ambiguous origin.')
        result = next(iter(matches.values())).validate(), sha256
        self.verified[path] = result
        return result

    def _notice(self, package):
        candidates = [package] + [p for p in self.archived + list(self.installed.values())
                                  if p is not package and (p.source, p.source_version) ==
                                  (package.source, package.source_version)]
        roots = ([self.extracted_root] if self.extracted_root else []) + [self.system_root]
        for candidate in candidates:
            for root in roots:
                path = root / 'usr/share/doc' / candidate.name / 'copyright'
                try:
                    owner, sha256 = self._resolve(path)
                except (OSError, RuntimeError):
                    continue
                if (owner.source, owner.source_version) == (package.source, package.source_version):
                    data = path.read_bytes()
                    if data and hashlib.sha256(data).hexdigest() == sha256:
                        return data, sha256
        raise RuntimeError('No verified copyright notice for the exact source version.')

    def stage(self, origins, bundle):
        bundle = Path(bundle).resolve()
        for name in origins:
            if not safe_relative(name):
                raise RuntimeError('Unsafe runtime destination path.')
        recorded = set(origins)
        for folder in ('usr/lib', 'usr/qml', 'usr/plugins'):
            for path in (bundle / folder).rglob('*'):
                if path.is_file() and path.relative_to(bundle).as_posix() not in recorded:
                    raise RuntimeError('An unrecorded runtime file has no package origin.')
        records, packages = [], {}
        for name, original in sorted(origins.items()):
            package, sha256 = self._resolve(original)
            staged = (bundle / name).resolve(strict=True)
            if not staged.is_relative_to(bundle) or fingerprint(staged)[1] != sha256:
                raise RuntimeError('The staged file differs from its verified package origin.')
            packages[package.key] = package
            records.append({'path': name, 'sha256': sha256, 'component': package.key})
        notices = {key: self._notice(package) for key, package in packages.items()}
        common = self.system_root / 'usr/share/common-licenses'
        for data, _ in notices.values():
            for name in re.findall(rb'/usr/share/common-licenses/([A-Za-z0-9.+-]+)', data):
                path = common / name.decode('ascii').rstrip('.,;:')
                if not path.is_file() or not path.stat().st_size:
                    raise RuntimeError('A referenced common license is missing.')
        common_files = []
        if packages:
            for path in sorted(common.iterdir()):
                if path.is_file():
                    try:
                        _, sha256 = self._resolve(path)
                        data = path.read_bytes()
                        if not data or hashlib.sha256(data).hexdigest() != sha256:
                            raise RuntimeError('Changed license bytes.')
                    except (OSError, RuntimeError) as error:
                        raise RuntimeError('No verified common license text.') from error
                    common_files.append((path.name, data, sha256))
        directory = bundle / 'usr/share/reword/licenses'
        components = []
        for key, package in sorted(packages.items()):
            relative = ('usr/share/reword/licenses/debian/' +
                        package.key.replace(':', '_').replace('=', '_') + '/copyright')
            target = bundle / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data, sha256 = notices[key]
            target.write_bytes(data)
            record = {'id': key, 'package': package.identifier, 'version': package.version,
                      'sourcePackage': package.source, 'sourceVersion': package.source_version,
                      'notice': relative, 'noticeSha256': sha256}
            if package.archive_sha256:
                record['archiveSha256'] = package.archive_sha256
            components.append(record)
        if packages:
            target = directory / 'common'
            target.mkdir(parents=True, exist_ok=True)
            for name, data, _ in common_files:
                (target / name).write_bytes(data)
            (directory / 'debian/README.txt').write_text(
                'Exact Debian package copyright notices accompany the runtime files.\n'
                'References to /usr/share/common-licenses are supplied in ../common/.\n'
                'Package/source versions and SHA256 fingerprints are in runtime-manifest.json.\n'
                'This notice inventory does not replace corresponding source archives.\n')
        return {'schemaVersion': 1, 'components': components, 'files': records,
                'commonLicenses': [
                    {'path': 'usr/share/reword/licenses/common/' + name, 'sha256': sha256}
                    for name, _, sha256 in common_files]}
