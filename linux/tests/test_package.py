import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
import zipfile
from unittest import mock

SCRIPT = Path(__file__).parents[1] / "bordify_package.py"
spec = importlib.util.spec_from_file_location("bordify_package", SCRIPT)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class DMGImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.dmg = self.root / "Demo.dmg"
        self.dmg.write_bytes(b"\0" * 512 + b"koly" + b"\0" * 508)
        self.bapp = self.root / "Demo.bapp"

    def fake_extract(self, image, target, sevenzip):
        app = target / "Demo.app" / "Contents"
        (app / "MacOS").mkdir(parents=True)
        (app / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleDisplayName": "Demo", "CFBundleIdentifier": "org.example.demo",
            "CFBundleExecutable": "Demo"}))
        (app / "MacOS" / "Demo").write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 128)
        (app / "Resources").mkdir()
        (app / "Resources" / "data.txt").write_text("hello", encoding="utf-8")

    def test_dmg_converts_to_bapp_and_reads_metadata_without_execution(self):
        with mock.patch.object(package, "_extract", side_effect=self.fake_extract):
            got = package.import_dmg(self.dmg, self.bapp, sevenzip="7z")
        self.assertEqual(got, self.bapp)
        m = package.inspect_package(got)
        self.assertEqual(m["format"], "org.bordify.bapp")
        self.assertEqual(m["name"], "Demo")
        self.assertFalse(m["launchable"])
        self.assertEqual(m["runtime"], "macos-macho-unavailable")
        with zipfile.ZipFile(got) as archive:
            self.assertEqual(archive.read("payload/Demo.app/Contents/Resources/data.txt"), b"hello")

    def test_dmg_requires_real_udif_footer(self):
        self.dmg.write_bytes(b"not a dmg")
        with self.assertRaisesRegex(package.PackageError, "DMG"):
            package.import_dmg(self.dmg, self.bapp, sevenzip="7z")

    def test_output_never_overwrites_existing_app(self):
        self.bapp.write_bytes(b"keep this")
        with self.assertRaisesRegex(package.PackageError, "exists"):
            package.import_dmg(self.dmg, self.bapp, sevenzip="7z")
        self.assertEqual(self.bapp.read_bytes(), b"keep this")

    def test_macho_missing_is_rejected(self):
        def bad_extract(image, target, sevenzip):
            self.fake_extract(image, target, sevenzip)
            (target / "Demo.app/Contents/MacOS/Demo").write_text("echo nope")
        with mock.patch.object(package, "_extract", side_effect=bad_extract):
            with self.assertRaisesRegex(package.PackageError, "Mach-O"):
                package.import_dmg(self.dmg, self.bapp, sevenzip="7z")

    def test_unsafe_extracted_symlink_is_rejected(self):
        with mock.patch.object(package, "_extract", side_effect=self.fake_extract):
            app = self.root / "test"
            app.mkdir()
            self.fake_extract(self.dmg, app, "7z")
            (app / "Demo.app" / "badlink").symlink_to("/etc/passwd")
            with self.assertRaisesRegex(package.PackageError, "unsupported link"):
                package._check_tree(app)

    def test_package_detects_payload_modification(self):
        with mock.patch.object(package, "_extract", side_effect=self.fake_extract):
            package.import_dmg(self.dmg, self.bapp, sevenzip="7z")
        with zipfile.ZipFile(self.bapp, "a") as archive:
            # Duplicate zip members are rejected before checksums are considered.
            with self.assertWarns(UserWarning):
                archive.writestr("payload/Demo.app/Contents/Resources/data.txt", "tampered")
        with self.assertRaisesRegex(package.PackageError, "Duplicate"):
            package.inspect_package(self.bapp)

    def test_path_traversal_in_package_is_rejected(self):
        with zipfile.ZipFile(self.bapp, "w") as archive:
            archive.writestr("../escape", "x")
            archive.writestr("manifest.json", json.dumps({"format": package.FORMAT, "version": 1}))
        with self.assertRaisesRegex(package.PackageError, "unsafe package"):
            package.inspect_package(self.bapp)

    def test_preflight_blocks_relative_traversal(self):
        output = "Path = Demo.dmg\n----------\nPath = ../../escape\nSize = 10\n"
        with mock.patch.object(package.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=output)):
            with self.assertRaisesRegex(package.PackageError, "unsafe archive"):
                package._preflight_7z(self.dmg, "7z")

    def test_preflight_rejects_archive_links(self):
        output = "Path = Demo.dmg\n----------\nPath = Applications\nSymbolic Link = /Applications\n"
        with mock.patch.object(package.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=output)):
            with self.assertRaisesRegex(package.PackageError, "links"):
                package._preflight_7z(self.dmg, "7z")


if __name__ == "__main__":
    unittest.main()
