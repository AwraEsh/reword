#!/usr/bin/env python3
"""Download build dependencies with apt and unpack locally, without root."""
import pathlib, subprocess, re, os
root = pathlib.Path(__file__).resolve().parents[1]
prefix = root / '.deps'
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
