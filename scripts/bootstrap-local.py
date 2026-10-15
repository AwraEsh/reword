#!/usr/bin/env python3
"""Download build dependencies with apt and unpack locally, without root."""
import pathlib, subprocess, re, os
root = pathlib.Path(__file__).resolve().parents[1]
prefix = root / '.deps'
(prefix/'downloads').mkdir(parents=True, exist_ok=True)
packages = ['qt6-base-dev', 'qt6-declarative-dev', 'qt6-base-dev-tools',
 'qt6-declarative-dev-tools', 'qt6-qpa-plugins', 'libqt6sql6-sqlite',
 'qml6-module-qtquick', 'qml6-module-qtquick-controls', 'qml6-module-qtquick-layouts',
 'qml6-module-qtquick-window', 'qml6-module-qtqml-workerscript',
 'qml6-module-qtquick-templates', 'libhunspell-dev', 'libfcitx5core-dev',
 'libfcitx5config-dev', 'libfcitx5utils-dev']
seen = set()
queue = list(packages)
while queue:
    p = queue.pop(0)
    if p in seen: continue
    seen.add(p)
    installed = subprocess.run(['dpkg-query', '-W', '-f=${Status}', p], capture_output=True, text=True)
    if 'install ok installed' in installed.stdout: continue
    info = subprocess.run(['apt-cache', 'depends', p], capture_output=True, text=True, check=True).stdout
    for line in info.splitlines():
        m = re.match(r'\s*(?:\|)?(?:Pre)?Depends:\s*([^ <]+)', line)
        if m and ':' not in m[1]: queue.append(m[1])
    print('Download', p, flush=True)
    subprocess.run(['apt-get', 'download', p], cwd=prefix/'downloads', check=True, stdout=subprocess.DEVNULL)
for deb in (prefix/'downloads').glob('*.deb'):
    subprocess.run(['dpkg-deb', '-x', str(deb), str(prefix)], check=True)
print('Local prefix:', prefix)

# Debian package configs assume /usr. Rebase the downloaded development metadata.
local_usr = prefix / 'usr'
for pc in (local_usr/'lib/x86_64-linux-gnu/pkgconfig').glob('*.pc'):
    if not pc.is_symlink():
        pc.write_text(pc.read_text().replace('prefix=/usr\n', f'prefix={local_usr}\n'))
for cm in (local_usr/'lib/x86_64-linux-gnu/cmake').glob('Fcitx5*/*.cmake'):
    if not cm.is_symlink():
        cm.write_text(cm.read_text().replace('/usr/include/Fcitx5', str(local_usr/'include/Fcitx5')))
# Reuse already installed runtime libraries/tools; do not download another copy.
libdir = local_usr/'lib/x86_64-linux-gnu'
for pattern in ('libQt6*.so*', 'libFcitx5*.so*', 'libhunspell*.so*'):
    for source in pathlib.Path('/usr/lib/x86_64-linux-gnu').glob(pattern):
        dest = libdir/source.name
        if not dest.exists():
            if dest.is_symlink(): dest.unlink()
            dest.symlink_to(source)
for base in ('/usr/lib/qt6', '/usr/lib/x86_64-linux-gnu/qt6', '/usr/lib/x86_64-linux-gnu/cmake'):
    for source in pathlib.Path(base).rglob('*'):
        dest = prefix/str(source).lstrip('/')
        if dest.exists() or dest.is_symlink(): continue
        if source.is_dir(): dest.mkdir(parents=True, exist_ok=True)
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.symlink_to(source)
