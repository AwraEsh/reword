#!/usr/bin/env python3
"""Stage a self-contained app directory and a Debian installer.

Run in the same build environment as CMake. AppImage creation is optional when
APPIMAGETOOL points to a local appimagetool binary. No tool is downloaded/executed
implicitly by this script.
"""
import pathlib, subprocess, shutil, os, re, json
from package_audit import (dynamic_libraries, is_elf, minimum_glibc,
                           debian_host_dependencies, debian_dependencies)
from package_provenance import DebianProvenance
from package_qml import deploy_qml
root=pathlib.Path(__file__).resolve().parents[1]
build=root/'build'; dist=root/'dist'; dist.mkdir(exist_ok=True)
appdir=dist/'Reword.AppDir'
origins={}
def copy_runtime(source, destination):
 source, destination=pathlib.Path(source), pathlib.Path(destination)
 relative=destination.relative_to(appdir).as_posix()
 if relative in origins and origins[relative].resolve()!=source.resolve():
  raise RuntimeError('Conflicting runtime destination: '+relative)
 origins[relative]=source
 return shutil.copy2(source,destination)
if appdir.exists(): shutil.rmtree(appdir)
for name in ['usr/bin','usr/lib','usr/plugins','usr/qml','usr/share/reword/dictionaries','usr/share/reword/licenses']:
 (appdir/name).mkdir(parents=True,exist_ok=True)
shutil.copy2(build/'reword',appdir/'usr/bin/reword')
shutil.copy2(build/'reword-native-helper',appdir/'usr/bin/reword-native-helper')
qt_prefix=pathlib.Path(os.environ.get('REWORD_QT_PREFIX',str(root/'.deps/usr')))
if not qt_prefix.exists(): qt_prefix=pathlib.Path('/usr')
qt_lib=qt_prefix/'lib/x86_64-linux-gnu/qt6'
if not qt_lib.exists(): qt_lib=qt_prefix/'lib/qt6'
for plugin in ['platforms','sqldrivers','imageformats','iconengines','wayland-decoration-client','wayland-graphics-integration-client','wayland-shell-integration']:
 src=qt_lib/'plugins'/plugin
 if src.exists():shutil.copytree(src,appdir/'usr/plugins'/plugin,symlinks=False,dirs_exist_ok=True,copy_function=copy_runtime)
input_contexts=[]
for name in ['libcomposeplatforminputcontextplugin.so','libibusplatforminputcontextplugin.so','libfcitx5platforminputcontextplugin.so']:
 src=qt_lib/'plugins/platforminputcontexts'/name
 if src.is_file():
  target=appdir/'usr/plugins/platforminputcontexts'/name
  target.parent.mkdir(parents=True,exist_ok=True)
  copy_runtime(src,target);input_contexts.append(name)
qml_modules=deploy_qml(root,qt_lib/'qml',qt_prefix,appdir/'usr/qml',copy_runtime)
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
for binary in [path for path in appdir.rglob('*') if is_elf(path)]:
 for resolved in dynamic_libraries(binary):
  src=pathlib.Path(resolved);name=src.name
  if name in seen or skip.match(name):continue
  seen.add(name);copy_runtime(src,appdir/'usr/lib'/name)
# Include every shipped ELF and the addon that is loaded against host libraries.
addon=build/'libreword-fcitx.so'
objects=[path for path in appdir.rglob('*') if is_elf(path)]
addon_libraries=[]
addon_packages=[]
if addon.exists():
 objects.append(addon)
 addon_libraries=sorted(pathlib.Path(path).name for path in dynamic_libraries(addon))
 addon_packages=debian_host_dependencies(addon)
glibc=minimum_glibc(objects)
extracted=qt_prefix.resolve().parent if qt_prefix.resolve()!=pathlib.Path('/usr') else None
third_party=DebianProvenance(extracted_root=extracted,
 archives=sorted((extracted/'downloads').glob('*.deb')) if extracted else ()).stage(origins,appdir)
(appdir/'usr/share/reword/runtime-manifest.json').write_text(json.dumps({
 'minimumGlibc':glibc,'elfObjects':len(objects),'runtimeLibraries':sorted(seen),
 'fcitxAddon':addon.exists(),'fcitxHostLibraries':addon_libraries,
 'fcitxHostPackages':addon_packages,
 'thirdParty':third_party,
 'qmlModules':qml_modules,'inputContextPlugins':input_contexts,
},indent=2)+'\n')
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
dependencies=debian_dependencies(glibc,addon_packages)
if addon.exists():
 target=debroot/'usr/lib/x86_64-linux-gnu/fcitx5';target.mkdir(parents=True);shutil.copy2(addon,target/addon.name)
 shutil.copytree(root/'assets/dictionaries',debroot/'usr/share/reword/dictionaries',dirs_exist_ok=True)
 target=debroot/'usr/share/fcitx5/addon';target.mkdir(parents=True);shutil.copy2(root/'packaging/reword-fcitx.conf',target/'reword.conf')
 dependencies+=['fcitx5','libqt6sql6-sqlite']
dependencies=', '.join(dict.fromkeys(dependencies))
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
