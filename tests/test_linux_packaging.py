"""Exercise the package command without host Qt or application/user data."""
import os
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
        for folder in ("scripts", "packaging", "assets/dictionaries", "build", "tools", "qt/lib/qt6"):
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
        self.env = dict(os.environ, REWORD_QT_PREFIX=str(self.root / "qt"))
        self.env["PATH"] = str(self.root / "tools") + os.pathsep + os.environ["PATH"]
        self.env.pop("APPIMAGETOOL", None)
        self.tool("ldd", "print('linux-vdso.so.1 (0x1234)')")
        self.tool("readelf", "print(\"Version needs section '.gnu.version_r' contains 1 entry:\"); print('  Name: GLIBC_2.39 Flags: none Version: 2')")

    def tool(self, name, body):
        target = self.root / "tools" / name
        target.write_text("#!" + sys.executable + "\n" + body + "\n")
        target.chmod(0o755)

    def package(self):
        return subprocess.run([sys.executable, str(self.root / "scripts/package-linux.py")],
                              env=self.env, cwd=self.root, text=True, capture_output=True)

    def test_missing_runtime_library_rejects_package(self):
        self.tool("ldd", "print('libsynthetic-missing.so.1 => not found')")
        result = self.package()
        self.assertNotEqual(result.returncode, 0,
                            "The packager silently accepted an unresolved runtime dependency.")
        self.assertIn("libsynthetic-missing.so.1", result.stdout + result.stderr)
        self.assertFalse((self.root / "dist/reword_0.1.0_amd64.deb").exists())

    def test_plugin_requirements_count_but_provided_symbols_do_not(self):
        plugin = self.root / "qt/lib/qt6/plugins/platforms/libqsynthetic.so"
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"\x7fELFsynthetic plugin")
        self.tool("readelf", "import sys\nprint(\"Version needs section '.gnu.version_r' contains 1 entry:\")\nprint('  Name: GLIBC_' + ('2.41' if sys.argv[-1].endswith('libqsynthetic.so') else '2.17'))\nprint(\"Version definition section '.gnu.version_d' contains 1 entry:\")\nprint('  Name: GLIBC_99.99')")
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        depends = subprocess.check_output(["dpkg-deb", "-f", str(self.root / "dist/reword_0.1.0_amd64.deb"), "Depends"], text=True)
        self.assertIn("libc6 (>= 2.41)", depends)
        self.assertNotIn("99.99", depends)

    def test_versioned_plugin_dependency_is_validated(self):
        plugin = self.root / "qt/lib/qt6/plugins/platforms/libsynthetic.so.1"
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
