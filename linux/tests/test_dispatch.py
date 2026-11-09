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
        self.assertEqual(cmd, ["darling", "shell", "/Volumes/SystemRoot" + str(path)])

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

    def test_darling_pkg_installer(self):
        path = self.make_file("Installer.pkg", b"xar!" + bytes(16))
        cmd, env = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd[0:2], ["darling", "shell"])
        self.assertEqual(cmd, ["darling", "shell", "installer", "-pkg",
                               "/Volumes/SystemRoot" + str(path), "-target", "/"])
        self.assertEqual(env, {})

    def test_bad_pkg_rejected(self):
        path = self.make_file("Installer.pkg", b"not an installer")
        with self.assertRaises(launcher.LaunchError):
            launcher.plan_launch(path, available=self.available)

    def test_dmg_is_mount_not_execute(self):
        path = self.make_file("Installer.dmg", b"sample image")
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd, ["darling", "shell", "hdiutil", "attach",
                               "/Volumes/SystemRoot" + str(path)])

    def test_macho_metacharacters_are_single_argument(self):
        path = self.make_file("a $(echo BAD); test.macho", b"\xcf\xfa\xed\xfe" + bytes(8))
        cmd, env = launcher.plan_launch(path, available=self.available)
        self.assertEqual(cmd, ["darling", "shell", "/Volumes/SystemRoot" + str(path)])
        self.assertEqual(env, {})

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

    def test_launch_prepares_wine_prefix_parent(self):
        path = self.make_file("example.exe", b"MZ" + bytes(64))
        prefix = self.base / "new" / "prefix" / "wineprefix"
        with mock.patch.object(launcher, "plan_launch", return_value=(
                ["wine", str(path)], {"WINEPREFIX": str(prefix)})), \
             mock.patch.object(launcher.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(launcher.open_file(path), 0)
            self.assertTrue(prefix.parent.is_dir())
            self.assertEqual(run.call_args.kwargs["env"]["WINEPREFIX"], str(prefix))

    def test_no_shell_execution_of_metacharacters(self):
        path = self.make_file("a; touch EXPLOIT.exe", b"MZ" + bytes(40))
        cmd, _ = launcher.plan_launch(path, available=self.available)
        self.assertEqual(len(cmd), 2)
        self.assertEqual(cmd[1], str(path))

if __name__ == "__main__":
    unittest.main()
