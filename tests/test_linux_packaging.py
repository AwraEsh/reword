"""Exercise the package command without host Qt or application/user data."""
import os
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LinuxPackagingTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="reword-package-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for folder in ("scripts", "packaging", "assets/dictionaries", "build", "tools", ".deps/usr/lib/qt6"):
            (self.root / folder).mkdir(parents=True)
        for source in (ROOT / "scripts").glob("package*.py"):
            shutil.copy2(source, self.root / "scripts" / source.name)
        for name in ("en_US.aff", "en_US.dic", "fa_IR.aff", "fa_IR.dic"):
            (self.root / "assets/dictionaries" / name).write_text("synthetic fixture\n")
        for name in ("reword.desktop", "reword.svg"):
            (self.root / "assets" / name).write_text("synthetic fixture\n")
        for name in ("LICENSE", "THIRD_PARTY.md"):
            (self.root / name).write_text("synthetic fixture\n")
        (self.root / "packaging/reword-fcitx.conf").write_text("synthetic fixture\n")
        for name in ("reword", "reword-native-helper"):
            (self.root / "build" / name).write_bytes(b"\x7fELFsynthetic executable")
        self.env = dict(os.environ, REWORD_QT_PREFIX=str(self.root / ".deps/usr"))
        self.env["PATH"] = str(self.root / "tools") + os.pathsep + os.environ["PATH"]
        self.env.pop("APPIMAGETOOL", None)
        self.tool("ldd", "print('linux-vdso.so.1 (0x1234)')")
        self.tool("dpkg-shlibdeps", "print('shlibs:Depends=libc6 (>= 2.17), libqt6core6 (>= 6.4.2), libstdc++6 (>= 12), libfcitx5core7 (>= 5.0.21)')")
        self.tool("readelf", "print(\"Version needs section '.gnu.version_r' contains 1 entry:\"); print('  Name: GLIBC_2.39 Flags: none Version: 2')")

    def tool(self, name, body):
        target = self.root / "tools" / name
        target.write_text("#!" + sys.executable + "\n" + body + "\n")
        target.chmod(0o755)

    def package(self, indexed=True, notice=True, modified=False):
        prefix = self.root / '.deps/usr'
        runtime = [path for path in prefix.rglob('*') if path.is_file()]
        if runtime and indexed:
            copyright = prefix / 'share/doc/reword-synthetic-runtime/copyright'
            copyright.parent.mkdir(parents=True, exist_ok=True)
            if notice:
                copyright.write_text('Synthetic runtime copyright.\nLicense: test fixture only.\n')
            stage = self.root / 'fixture-deb'
            shutil.copytree(prefix, stage / 'usr')
            (stage / 'DEBIAN').mkdir()
            (stage / 'DEBIAN/control').write_text(
                'Package: reword-synthetic-runtime\nVersion: 1.0-1+b1\n'
                'Source: reword-synthetic-runtime-source (1.0-1)\nArchitecture: amd64\n'
                'Maintainer: Fixture <fixture@example.invalid>\nDescription: synthetic fixture\n')
            sums = []
            for path in (stage / 'usr').rglob('*'):
                if path.is_file():
                    sums.append(hashlib.md5(path.read_bytes()).hexdigest() + '  ' +
                                path.relative_to(stage).as_posix())
            (stage / 'DEBIAN/md5sums').write_text('\n'.join(sums) + '\n')
            archives = self.root / '.deps/downloads'
            archives.mkdir()
            subprocess.run(['dpkg-deb', '--build', str(stage), str(archives / 'fixture.deb')],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if modified:
                runtime[0].write_bytes(runtime[0].read_bytes() + b'modified')
        return subprocess.run([sys.executable, str(self.root / "scripts/package-linux.py")],
                              env=self.env, cwd=self.root, text=True, capture_output=True)

    def scanner(self, entries):
        self.tool('qmlimportscanner', 'print(' + repr(json.dumps(entries)) + ')')
        self.env['REWORD_QMLIMPORTSCANNER'] = str(self.root / 'tools/qmlimportscanner')

    def test_only_scanned_qml_modules_and_their_assets_are_copied(self):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\nimport QtQuick.Controls\n')
        base = self.root / '.deps/usr/lib/qt6/qml'
        entries = []
        for name in ('QtQuick', 'QtQuick.Controls', 'QtQuick.Controls.Basic', 'QtQuick.VirtualKeyboard'):
            relative = name.replace('.', '/')
            folder = base / relative
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'qmldir').write_text('module ' + name + '\n')
            (folder / 'file.qml').write_text('synthetic ' + name)
            if name != 'QtQuick.VirtualKeyboard':
                entries.append({'name': name, 'type': 'module', 'relativePath': relative, 'path': str(folder)})
        asset = base / 'QtQuick/Controls/Basic/images/icon.dat'
        asset.parent.mkdir()
        asset.write_bytes(b'synthetic image')
        self.scanner(entries)
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        app = self.root / 'dist/Reword.AppDir'
        self.assertTrue((app / 'usr/qml/QtQuick/Controls/Basic/file.qml').exists())
        self.assertTrue((app / 'usr/qml/QtQuick/Controls/Basic/images/icon.dat').exists())
        self.assertFalse((app / 'usr/qml/QtQuick/VirtualKeyboard').exists())
        manifest = json.loads((app / 'usr/share/reword/runtime-manifest.json').read_text())
        self.assertEqual(set(manifest['qmlModules']), {entry['name'] for entry in entries})

    def test_unresolved_required_qml_import_rejects_package(self):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\n')
        (self.root / '.deps/usr/lib/qt6/qml').mkdir()
        self.scanner([{'name': 'QtQuick', 'type': 'module'}])
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unresolved QML', result.stdout + result.stderr)

    def test_unsafe_qml_scanner_path_rejects_package(self):
        (self.root / 'qml').mkdir()
        (self.root / '.deps/usr/lib/qt6/qml').mkdir()
        self.scanner([{'name': 'QtQuick', 'type': 'module', 'relativePath': '../escape', 'path': str(self.root)}])
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('QML module path', result.stdout + result.stderr)

    def test_absent_declared_optional_style_is_allowed(self):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\n')
        module = self.root / '.deps/usr/lib/qt6/qml/QtQuick'
        module.mkdir(parents=True)
        (module / 'qmldir').write_text('module QtQuick\noptional import QtQuick.NativeOnly auto\n')
        self.scanner([{'name': 'QtQuick', 'type': 'module', 'relativePath': 'QtQuick', 'path': str(module)},
                      {'name': 'QtQuick.NativeOnly', 'type': 'module'}])
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_direct_import_cannot_hide_behind_an_optional_declaration(self):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\nimport QtQuick.NativeOnly\n')
        module = self.root / '.deps/usr/lib/qt6/qml/QtQuick'
        module.mkdir(parents=True)
        (module / 'qmldir').write_text('module QtQuick\noptional import QtQuick.NativeOnly auto\n')
        self.scanner([{'name': 'QtQuick', 'type': 'module', 'relativePath': 'QtQuick', 'path': str(module)},
                      {'name': 'QtQuick.NativeOnly', 'type': 'module'}])
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unresolved QML', result.stdout + result.stderr)

    def test_malformed_qml_scan_rejects_package(self):
        (self.root / 'qml').mkdir()
        self.scanner([])
        self.tool('qmlimportscanner', "print('not JSON')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid QML', result.stdout + result.stderr)

    def test_qml_scanner_cannot_select_an_external_directory(self):
        (self.root / 'qml').mkdir()
        external = self.root / 'external'
        external.mkdir()
        (external / 'qmldir').write_text('module QtQuick\n')
        self.scanner([{'name': 'QtQuick', 'type': 'module', 'relativePath': 'QtQuick', 'path': str(external)}])
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('QML module path', result.stdout + result.stderr)

    def mixed_transitive_import(self, qml_file=False):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\nimport QtQuick.Controls\n')
        base = self.root / '.deps/usr/lib/qt6/qml'
        entries = []
        for name in ('QtQuick', 'QtQuick.Controls'):
            folder = base / name.replace('.', '/')
            folder.mkdir(parents=True, exist_ok=True)
            contents = 'module ' + name + '\n'
            if name == 'QtQuick':
                contents += 'optional import QtQuick.RequiredElsewhere auto\n'
            elif qml_file:
                (folder / 'Required.qml').write_text('import QtQuick.RequiredElsewhere\n')
            else:
                contents += 'depends QtQuick.RequiredElsewhere auto\n'
            (folder / 'qmldir').write_text(contents)
            entries.append({'name': name, 'type': 'module', 'relativePath': name.replace('.', '/'), 'path': str(folder)})
        entries.append({'name': 'QtQuick.RequiredElsewhere', 'type': 'module'})
        self.scanner(entries)
        return self.package()

    def test_required_transitive_qmldir_import_overrides_optional_use_elsewhere(self):
        result = self.mixed_transitive_import()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unresolved QML', result.stdout + result.stderr)

    def test_required_transitive_qml_import_overrides_optional_use_elsewhere(self):
        result = self.mixed_transitive_import(qml_file=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unresolved QML', result.stdout + result.stderr)

    def test_designer_files_are_not_runtime_dependencies(self):
        (self.root / 'qml').mkdir()
        (self.root / 'qml/Main.qml').write_text('import QtQuick\n')
        folder = self.root / '.deps/usr/lib/qt6/qml/QtQuick'
        (folder / 'designer').mkdir(parents=True)
        (folder / 'qmldir').write_text('module QtQuick\n')
        (folder / 'designer/DesignerOnly.qml').write_text('import HelperWidgets\n')
        self.scanner([{'name': 'QtQuick', 'type': 'module', 'relativePath': 'QtQuick', 'path': str(folder)}])
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / 'dist/Reword.AppDir/usr/qml/QtQuick/designer').exists())

    def test_only_supported_input_context_providers_are_bundled(self):
        folder = self.root / '.deps/usr/lib/qt6/plugins/platforminputcontexts'
        folder.mkdir(parents=True)
        expected = {'libcomposeplatforminputcontextplugin.so', 'libibusplatforminputcontextplugin.so',
                    'libfcitx5platforminputcontextplugin.so'}
        extra = {'libuimplatforminputcontextplugin.so', 'libfcitxplatforminputcontextplugin-qt6.so',
                 'libqtvirtualkeyboardplugin.so'}
        for name in expected | extra:
            (folder / name).write_bytes(b'\x7fELFsynthetic input context')
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        staged = self.root / 'dist/Reword.AppDir/usr/plugins/platforminputcontexts'
        self.assertEqual({p.name for p in staged.iterdir()}, expected)

    def test_runtime_provenance_and_notices_cover_plugin_and_dependency(self):
        plugin = self.root / '.deps/usr/lib/qt6/plugins/platforms/libqsynthetic.so'
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b'\x7fELFsynthetic plugin')
        dependency = self.root / '.deps/usr/lib/libsyntheticdep.so.1'
        dependency.write_bytes(b'\x7fELFsynthetic library')
        self.tool('ldd', "print('libsyntheticdep.so.1 => " + str(dependency) + " (0x1234)')")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        appdir = self.root / 'dist/Reword.AppDir'
        manifest = json.loads((appdir / 'usr/share/reword/runtime-manifest.json').read_text())
        third_party = manifest['thirdParty']
        self.assertEqual(len(third_party['files']), 2)
        self.assertEqual(third_party['components'][0]['sourceVersion'], '1.0-1')
        self.assertNotIn(str(self.root), json.dumps(manifest))
        for record in third_party['files']:
            self.assertEqual(record['sha256'], hashlib.sha256((appdir / record['path']).read_bytes()).hexdigest())
        component = third_party['components'][0]
        self.assertEqual((appdir / component['notice']).read_text(),
                         'Synthetic runtime copyright.\nLicense: test fixture only.\n')

    def test_unknown_runtime_origin_rejects_before_installer_creation(self):
        plugin = self.root / '.deps/usr/lib/qt6/plugins/platforms/libqsynthetic.so'
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b'\x7fELFsynthetic plugin')
        result = self.package(indexed=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('origin', result.stdout + result.stderr)
        self.assertFalse((self.root / 'dist/reword_0.1.0_amd64.deb').exists())

    def test_missing_runtime_notice_rejects_before_installer_creation(self):
        plugin = self.root / '.deps/usr/lib/qt6/plugins/platforms/libqsynthetic.so'
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b'\x7fELFsynthetic plugin')
        result = self.package(notice=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('notice', result.stdout + result.stderr)
        self.assertFalse((self.root / 'dist/reword_0.1.0_amd64.deb').exists())

    def test_modified_runtime_rejects_before_installer_creation(self):
        plugin = self.root / '.deps/usr/lib/qt6/plugins/platforms/libqsynthetic.so'
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b'\x7fELFsynthetic plugin')
        result = self.package(modified=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum', result.stdout + result.stderr)
        self.assertFalse((self.root / 'dist/reword_0.1.0_amd64.deb').exists())

    def test_missing_runtime_library_rejects_package(self):
        self.tool("ldd", "print('libsynthetic-missing.so.1 => not found')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0,
                            "The packager silently accepted an unresolved runtime dependency.")
        self.assertIn("libsynthetic-missing.so.1", result.stdout + result.stderr)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_plugin_requirements_count_but_provided_symbols_do_not(self):
        plugin = self.root / ".deps/usr/lib/qt6/plugins/platforms/libqsynthetic.so"
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"\x7fELFsynthetic plugin")
        self.tool("readelf", "import sys\nprint(\"Version needs section '.gnu.version_r' contains 1 entry:\")\nprint('  Name: GLIBC_' + ('2.41' if sys.argv[-1].endswith('libqsynthetic.so') else '2.17'))\nprint(\"Version definition section '.gnu.version_d' contains 1 entry:\")\nprint('  Name: GLIBC_99.99')")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        depends = subprocess.check_output(["dpkg-deb", "-f", str(self.root / "dist/reword_0.1.0_amd64.deb"), "Depends"], text=True)
        self.assertIn("libc6 (>= 2.41)", depends)
        self.assertNotIn("99.99", depends)

    def test_versioned_plugin_dependency_is_validated(self):
        plugin = self.root / ".deps/usr/lib/qt6/plugins/platforms/libsynthetic.so.1"
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"\x7fELFsynthetic versioned module")
        self.tool("ldd", "import sys\nprint('libversioned-missing.so.1 => not found' if sys.argv[-1].endswith('libsynthetic.so.1') else 'linux-vdso.so.1 (0x1234)')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libversioned-missing.so.1", result.stdout + result.stderr)

    def test_missing_addon_dependency_rejects_package(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        self.tool("ldd", "import sys\nprint('libmissing-addon.so.1 => not found' if sys.argv[-1].endswith('libreword-fcitx.so') else 'linux-vdso.so.1 (0x1234)')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0,
                            "The separately installed addon has unresolved host dependencies.")
        self.assertIn("libmissing-addon.so.1", result.stdout + result.stderr)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_dependency_inspection_uses_stable_locale(self):
        self.env["LC_ALL"] = "de_DE.UTF-8"
        self.tool("ldd", "import os\nprint('liblocalized-missing.so.1 => not found' if os.environ.get('LC_ALL') == 'C' else 'liblocalized-missing.so.1 => nicht gefunden')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("liblocalized-missing.so.1", result.stdout + result.stderr)

    def test_host_addon_requirement_is_included(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        self.tool("readelf", "import sys\nprint(\"Version needs section '.gnu.version_r' contains 1 entry:\")\nprint('  Name: GLIBC_' + ('2.42' if sys.argv[-1].endswith('libreword-fcitx.so') else '2.17'))")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        depends = subprocess.check_output(["dpkg-deb", "-f", str(self.root / "dist/reword_0.1.0_amd64.deb"), "Depends"], text=True)
        self.assertIn("libc6 (>= 2.42)", depends)

    def test_addon_host_dependencies_come_from_debian_symbols(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        depends = subprocess.check_output(["dpkg-deb", "-f", str(self.root / "dist/reword_0.1.0_amd64.deb"), "Depends"], text=True)
        self.assertIn("libqt6core6 (>= 6.4.2)", depends)
        self.assertNotIn("6.8", depends)
        self.assertIn("libstdc++6 (>= 12)", depends)
        self.assertIn("libfcitx5core7 (>= 5.0.21)", depends)
        self.assertIn("libc6 (>= 2.39)", depends)
        self.assertNotIn("libc6 (>= 2.17)", depends)
        self.assertIn("libqt6sql6-sqlite", depends)

    def test_missing_debian_dependency_metadata_rejects_addon(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        self.tool("dpkg-shlibdeps", "import sys; sys.exit(1)")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_malformed_debian_dependency_output_rejects_addon(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        self.tool("dpkg-shlibdeps", "print('unexpected output')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_debian_symbols_can_raise_libc_package_minimum(self):
        (self.root / "build/libreword-fcitx.so").write_bytes(b"\x7fELFsynthetic addon")
        self.tool("dpkg-shlibdeps", "print('shlibs:Depends=libc6 (>= 2.44), libstdc++6 (>= 15)')")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        depends = subprocess.check_output(["dpkg-deb", "-f", str(self.root / "dist/reword_0.1.0_amd64.deb"), "Depends"], text=True)
        self.assertIn("libc6 (>= 2.44)", depends)
        self.assertNotIn("libc6 (>= 2.39)", depends)

    def test_addon_metadata_inspects_actual_binary_in_stable_locale(self):
        addon = self.root / "build/libreword-fcitx.so"
        addon.write_bytes(b"\x7fELFsynthetic addon")
        self.env["LC_ALL"] = "de_DE.UTF-8"
        self.tool("dpkg-shlibdeps", "import os, pathlib, sys\n"
                  "assert os.environ.get('LC_ALL') == 'C'\n"
                  "assert '-O' in sys.argv and '--ignore-missing-info' not in sys.argv\n"
                  "assert " + repr("-e" + str(addon)) + " in sys.argv\n"
                  "print('shlibs:Depends=libc6 (>= 2.17), libstdc++6 (>= 12)')")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_addon_does_not_require_host_dependency_metadata(self):
        self.tool("dpkg-shlibdeps", "import sys; sys.exit(1)")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unreadable_version_requirements_reject_package(self):
        self.tool("readelf", "import sys; sys.exit(1)")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_debian_minimum_glibc_matches_all_staged_objects(self):
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        package = self.root / "dist/reword_0.1.0_amd64.deb"
        depends = subprocess.check_output(["dpkg-deb", "-f", str(package), "Depends"], text=True)
        self.assertIn("libc6 (>= 2.39)", depends)


if __name__ == "__main__":
    unittest.main()
