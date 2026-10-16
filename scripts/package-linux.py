#!/usr/bin/env python3
"""Stage a self-contained app directory and a Debian installer.

Run in the same build environment as CMake. AppImage creation is optional when
APPIMAGETOOL points to a local appimagetool binary. No tool is downloaded/executed
implicitly by this script.
"""
import pathlib, subprocess, shutil, os, re, json
root=pathlib.Path(__file__).resolve().parents[1]
build=root/'build'; dist=root/'dist'; dist.mkdir(exist_ok=True)
appdir=dist/'Reword.AppDir'
if appdir.exists(): shutil.rmtree(appdir)
for name in ['usr/bin','usr/lib','usr/plugins','usr/qml','usr/share/reword/dictionaries','usr/share/reword/licenses']:
 (appdir/name).mkdir(parents=True,exist_ok=True)
shutil.copy2(build/'reword',appdir/'usr/bin/reword')
shutil.copy2(build/'reword-native-helper',appdir/'usr/bin/reword-native-helper')
qt_prefix=pathlib.Path(os.environ.get('REWORD_QT_PREFIX',str(root/'.deps/usr')))
if not qt_prefix.exists(): qt_prefix=pathlib.Path('/usr')
qt_lib=qt_prefix/'lib/x86_64-linux-gnu/qt6'
if not qt_lib.exists(): qt_lib=qt_prefix/'lib/qt6'
for plugin in ['platforms','platforminputcontexts','sqldrivers','imageformats','iconengines','wayland-decoration-client','wayland-graphics-integration-client','wayland-shell-integration']:
 src=qt_lib/'plugins'/plugin
 if src.exists():shutil.copytree(src,appdir/'usr/plugins'/plugin,symlinks=False,dirs_exist_ok=True)
for module in ['QtQuick','QtQml','QtCore','QML']:
 src=qt_lib/'qml'/module
 if src.exists():shutil.copytree(src,appdir/'usr/qml'/module,symlinks=False,dirs_exist_ok=True)
# Use checked-in, pinned dictionary resources when available.
for name in ['en_US.aff','en_US.dic','fa_IR.aff','fa_IR.dic']:
 source=root/'assets/dictionaries'/name
 if not source.exists():raise SystemExit('Fetch licensed dictionaries first: python3 scripts/fetch-dictionaries.py')
 shutil.copy2(source,appdir/'usr/share/reword/dictionaries'/name)
for source in (root/'assets/dictionaries').glob('*frequency.json'):
 shutil.copy2(source,appdir/'usr/share/reword/dictionaries'/source.name)
for source in (root/'assets/dictionaries').glob('wordfreq-NOTICE.md'):
 shutil.copy2(source,appdir/'usr/share/reword/licenses'/source.name)
for source in (root/'assets/dictionaries').glob('*license*'):
 shutil.copy2(source,appdir/'usr/share/reword/licenses'/source.name)
for source in [root/'THIRD_PARTY.md',root/'LICENSE']:
 shutil.copy2(source,appdir/'usr/share/reword/licenses'/source.name)
# Resolve the full dynamic closure, including plugins loaded only at runtime.
skip=re.compile(r'^(libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libresolv\.so|ld-linux)')
seen=set()
for binary in [appdir/'usr/bin/reword',appdir/'usr/bin/reword-native-helper',*list((appdir/'usr/plugins').rglob('*.so')),*list((appdir/'usr/qml').rglob('*.so'))]:
 output=subprocess.run(['ldd',str(binary)],capture_output=True,text=True,check=True).stdout
 for line in output.splitlines():
  m=re.search(r'=> (/\S+)',line)
  if not m:continue
  src=pathlib.Path(m[1]);name=src.name
  if name in seen or skip.match(name):continue
  seen.add(name);shutil.copy2(src,appdir/'usr/lib'/name)
shutil.copy2(root/'assets/reword.desktop',appdir/'reword.desktop')
shutil.copy2(root/'assets/reword.svg',appdir/'reword.svg')
(appdir/'AppRun').write_text('''#!/bin/sh
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML_IMPORT_PATH="$HERE/usr/qml"
export QT_QPA_PLATFORMTHEME=
exec "$HERE/usr/bin/reword" "$@"
''')
(appdir/'AppRun').chmod(0o755)
# Debian installs the same bundled GUI; its separately loaded Fcitx addon uses host Qt.
debroot=dist/'deb-root'
if debroot.exists():shutil.rmtree(debroot)
shutil.copytree(appdir,debroot/'opt/reword',symlinks=False)
(debroot/'usr/bin').mkdir(parents=True);(debroot/'usr/bin/reword').symlink_to('/opt/reword/AppRun')
(debroot/'usr/share/applications').mkdir(parents=True);shutil.copy2(root/'assets/reword.desktop',debroot/'usr/share/applications/reword.desktop')
(debroot/'usr/share/icons/hicolor/scalable/apps').mkdir(parents=True);shutil.copy2(root/'assets/reword.svg',debroot/'usr/share/icons/hicolor/scalable/apps/reword.svg')
addon=build/'libreword-fcitx.so';dependencies='libc6 (>= 2.38), libgl1, libegl1'
if addon.exists():
 target=debroot/'usr/lib/x86_64-linux-gnu/fcitx5';target.mkdir(parents=True);shutil.copy2(addon,target/addon.name)
 shutil.copytree(root/'assets/dictionaries',debroot/'usr/share/reword/dictionaries',dirs_exist_ok=True)
 target=debroot/'usr/share/fcitx5/addon';target.mkdir(parents=True);shutil.copy2(root/'packaging/reword-fcitx.conf',target/'reword.conf')
 dependencies+=', fcitx5 (>= 5.1), libqt6core6t64 (>= 6.8), libqt6sql6 (>= 6.8), libqt6dbus6 (>= 6.8), libqt6sql6-sqlite, libhunspell-1.7-0'
(debroot/'DEBIAN').mkdir();(debroot/'DEBIAN/control').write_text(f'''Package: reword
Version: 0.1.0
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Reword contributors
Depends: {dependencies}
Description: Offline spelling assistant for English and Persian
 Corrects text in permitted plain text fields and provides a writing workspace.
''')
subprocess.run(['dpkg-deb','--root-owner-group','--build',str(debroot),str(dist/'reword_0.1.0_amd64.deb')],check=True)
if os.environ.get('APPIMAGETOOL'):
 env=dict(os.environ,ARCH='x86_64',VERSION='0.1.0')
 subprocess.run([env['APPIMAGETOOL'],str(appdir),str(dist/'Reword-0.1.0-x86_64.AppImage')],check=True,env=env)
print('Packages staged in',dist)
