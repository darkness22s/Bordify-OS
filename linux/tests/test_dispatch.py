#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).parents[1] / "bordify_open.py"
spec = importlib.util.spec_from_file_location("bordify_open", SCRIPT)
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class DispatchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.available = lambda name: "/usr/bin/" + name

    def make_file(self, name, data):
        p = self.base / name
        p.write_bytes(data)
        return p

    def test_windows_exe(self):
        path = self.make_file("app.exe", b"MZ" + bytes(100))
        cmd, env = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd, ["wine", str(path)])
        self.assertIn("WINEPREFIX", env)

    def test_windows_msi(self):
        path = self.make_file("install.msi", launcher.MSI_MAGIC + bytes(24))
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd[:3], ["wine", "msiexec", "/i"])

    def test_invalid_exe_rejected(self):
        path = self.make_file("fake.exe", b"not an EXE")
        with self.assertRaises(launcher.LaunchError):
            launcher.plan_launch(path, available=self.available)

    def test_missing_wine_rejected(self):
        path = self.make_file("app.exe", b"MZ" + bytes(40))
        with self.assertRaisesRegex(launcher.LaunchError, "Wine"):
            launcher.plan_launch(path, available=lambda _: None)

    def test_macho_with_spaces_and_quotes(self):
        path = self.make_file("my ' app.macho", b"\xcf\xfa\xed\xfe" + bytes(8))
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd[0:2], ["darling", "shell"])
        self.assertIn("'/Volumes/SystemRoot", cmd[2])
        self.assertIn("exec ", cmd[2])

    def test_bundle_validates_plist(self):
        app = self.base / "Example.app"
        folder = app / "Contents" / "MacOS"
        folder.mkdir(parents=True)
        (app / "Contents" / "Info.plist").write_bytes(
            plistlib.dumps({"CFBundleExecutable": "hello"}))
        binary = folder / "hello"
        binary.write_bytes(b"\xcf\xfa\xed\xfe" + bytes(8))
        cmd, _ = launcher.plan_launch(app, available=self.available)
        self.assertEqual(cmd[:2], ["darling", "shell"])
        self.assertIn("hello", cmd[-1])

    def test_bundle_path_traversal_rejected(self):
        app = self.base / "Example.app"
        (app / "Contents" / "MacOS").mkdir(parents=True)
        (app / "Contents" / "Info.plist").write_bytes(
            plistlib.dumps({"CFBundleExecutable": "../../wrong"}))
        with self.assertRaises(launcher.LaunchError):
            launcher.plan_launch(app, available=self.available)

    def test_dmg_is_mount_not_execute(self):
        path = self.make_file("Installer.dmg", b"sample image")
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertIn("hdiutil attach", cmd[2])

    def test_elf_native(self):
        path = self.make_file("local", b"\x7fELF" + bytes(32))
        path.chmod(0o700)
        cmd, env = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd, [str(path)])
        self.assertEqual(env, {})

    def test_text_uses_default_opener(self):
        path = self.make_file("notes.txt", b"hello")
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd, ["xdg-open", str(path)])

    def test_no_shell_execution_of_metacharacters(self):
        path = self.make_file("a; touch EXPLOIT.exe", b"MZ" + bytes(40))
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(len(cmd), 2)
        self.assertEqual(cmd[1], str(path))

if __name__ == "__main__":
    unittest.main()
