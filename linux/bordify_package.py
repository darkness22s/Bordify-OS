#!/usr/bin/env python3
"""Offline DMG -> Bordify .bapp import; never runs macOS executable code.

.bapp v1 is a ZIP archive of manifest.json and payload/<bundle>.app. This is
an archive/metadata format, not a compiled-binary translator or a sandbox.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

FORMAT = "org.bordify.bapp"
VERSION = 1
MAX_SOURCE = 4 * 1024**3
MAX_UNPACKED = 2 * 1024**3
MAX_FILES = 30000
MAX_METADATA = 256 * 1024


class PackageError(Exception):
    pass


def _digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _safe_name(name: str) -> bool:
    if not name or "\\" in name or "\x00" in name or name.startswith("/"):
        return False
    parts = PurePosixPath(name).parts
    return bool(parts) and all(part not in {".", ".."} for part in parts) and ":" not in parts[0]


def _check_tree(root: Path) -> list[Path]:
    files = []
    total = 0
    for base, dirs, names in os.walk(root, followlinks=False):
        for name in dirs + names:
            path = Path(base) / name
            rel = path.relative_to(root).as_posix()
            if not _safe_name(rel):
                raise PackageError(f"Unsafe extracted pathname: {rel!r}")
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                raise PackageError(f"DMG contains an unsupported link or special file: {rel}")
            if stat.S_ISREG(mode):
                total += path.stat().st_size
                files.append(path)
                if total > MAX_UNPACKED or len(files) > MAX_FILES:
                    raise PackageError("DMG exceeds Bordify import size/file-count limits")
    return files


def _preflight_7z(image: Path, sevenzip: str) -> None:
    """Check archive entry names before extraction (7z is not a sandbox)."""
    try:
        result = subprocess.run([sevenzip, "l", "-slt", str(image)],
                                capture_output=True, text=True, errors="replace",
                                timeout=120, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise PackageError(f"7-Zip could not inspect image: {exc}") from exc
    if result.returncode != 0:
        raise PackageError("7-Zip cannot read this disk image (unsupported, encrypted or corrupt)")
    # 7z prints the archive's own Path before its separator. Inspect entries only.
    body = result.stdout.split("----------", 1)
    if len(body) != 2:
        raise PackageError("7-Zip returned no readable archive entries")
    for line in body[1].splitlines():
        if line.startswith("Path = ") and not _safe_name(line[7:]):
            raise PackageError("DMG contains an unsafe archive entry path")
        if line.startswith(("Symbolic Link = ", "Hard Link = ")):
            raise PackageError("DMG contains links; safe link import is not supported")


def _extract(image: Path, target: Path, sevenzip: str) -> None:
    _preflight_7z(image, sevenzip)
    try:
        result = subprocess.run([sevenzip, "x", "-y", "-bd", "-o" + str(target), str(image)],
                                capture_output=True, text=True, errors="replace",
                                timeout=300, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise PackageError(f"7-Zip extraction failed: {exc}") from exc
    if result.returncode != 0:
        raise PackageError("7-Zip failed to extract the disk image")
    _check_tree(target)


def _bundle(root: Path, sevenzip: str) -> Path:
    def find() -> list[Path]:
        return sorted(p for p in root.rglob("*.app") if p.is_dir() and
                      (p / "Contents" / "Info.plist").is_file())
    candidates = find()
    if not candidates:
        # Some UDIF images unpack to a nested HFS/APFS image. Allow one more layer.
        images = [p for p in root.rglob("*") if p.is_file() and
                  p.suffix.lower() in {".hfs", ".apfs", ".img"}]
        for n, image in enumerate(images[:3]):
            dest = root / f"_bordify_volume_{n}"
            dest.mkdir()
            _extract(image, dest, sevenzip)
        candidates = find()
    if len(candidates) != 1:
        raise PackageError(f"Expected exactly one .app bundle in DMG, found {len(candidates)}")
    return candidates[0]


def _metadata(bundle: Path) -> dict:
    info = bundle / "Contents" / "Info.plist"
    if info.stat().st_size > MAX_METADATA:
        raise PackageError("Info.plist too large")
    try:
        with info.open("rb") as stream:
            plist = plistlib.load(stream)
    except (OSError, ValueError, TypeError, plistlib.InvalidFileException) as exc:
        raise PackageError("Invalid macOS bundle Info.plist") from exc
    if not isinstance(plist, dict):
        raise PackageError("Invalid macOS bundle metadata")
    executable = plist.get("CFBundleExecutable", "")
    if not isinstance(executable, str) or not executable or not _safe_name(executable) or "/" in executable:
        raise PackageError("Missing or unsafe CFBundleExecutable")
    program = bundle / "Contents" / "MacOS" / executable
    if not program.is_file() or program.is_symlink():
        raise PackageError("macOS bundle executable missing")
    with program.open("rb") as stream:
        magic = stream.read(4)
    if magic not in (b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
                     b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
                     b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                     b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"):
        raise PackageError("App contains no Mach-O executable (not a macOS .app)")
    name = plist.get("CFBundleDisplayName") or plist.get("CFBundleName") or bundle.stem
    identifier = plist.get("CFBundleIdentifier") or ""
    return {"name": str(name)[:200], "identifier": str(identifier)[:300],
            "executable": executable, "bundle_name": bundle.name}


def import_dmg(source: Path, output: Path | None = None, *, sevenzip: str | None = None) -> Path:
    source = source.expanduser().resolve(strict=True)
    if not source.is_file() or source.suffix.lower() != ".dmg":
        raise PackageError("Input must be a DMG file")
    size = source.stat().st_size
    if size < 512 or size > MAX_SOURCE:
        raise PackageError("Invalid or oversized DMG image")
    with source.open("rb") as stream:
        stream.seek(-512, os.SEEK_END)
        if stream.read(4) != b"koly":
            raise PackageError("Missing UDIF/DMG koly footer")
    sevenzip = sevenzip or shutil.which("7z")
    if not sevenzip:
        raise PackageError("7-Zip is required: install Debian package '7zip'")
    fingerprint = _digest(source)
    if output is None:
        output = Path.home() / ".local/share/bordify/apps" / (source.stem + "-" + fingerprint[:12] + ".bapp")
    output = output.expanduser().absolute()
    if output.suffix.lower() != ".bapp":
        raise PackageError("Output must use .bapp extension")
    if output.exists():
        raise PackageError(f"Output already exists (refusing overwrite): {output}")
    with tempfile.TemporaryDirectory(prefix="bordify-dmg-") as workspace:
        folder = Path(workspace) / "unpacked"
        folder.mkdir()
        _extract(source, folder, sevenzip)
        bundle = _bundle(folder, sevenzip)
        metadata = _metadata(bundle)
        files = [p for p in _check_tree(bundle) if p.is_file()]
        checksums = {}
        for file in files:
            member = "payload/" + bundle.name + "/" + file.relative_to(bundle).as_posix()
            if not _safe_name(member):
                raise PackageError("Unsafe archive filename")
            checksums[member] = _digest(file)
        manifest = {"format": FORMAT, "version": VERSION,
                    "name": metadata["name"], "identifier": metadata["identifier"],
                    "bundle_path": "payload/" + bundle.name,
                    "executable": metadata["executable"],
                    "source_type": "dmg", "source_sha256": fingerprint,
                    "launchable": False, "runtime": "macos-macho-unavailable",
                    "files": checksums}
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix=".bordify-", suffix=".tmp",
                                         dir=output.parent, delete=False) as temp:
            staging = Path(temp.name)
        try:
            with zipfile.ZipFile(staging, "w", allowZip64=True) as archive:
                archive.writestr("manifest.json", json.dumps(manifest, sort_keys=True, indent=2) + "\n")
                for file in files:
                    member = "payload/" + bundle.name + "/" + file.relative_to(bundle).as_posix()
                    archive.write(file, member, compress_type=zipfile.ZIP_DEFLATED)
            # Link into place without replacing a concurrently-created package.
            os.link(staging, output)
        except FileExistsError as exc:
            raise PackageError(f"Output already exists: {output}") from exc
        finally:
            staging.unlink(missing_ok=True)
    return output


def inspect_package(path: Path) -> dict:
    path = path.expanduser().resolve(strict=True)
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            if len(infos) > MAX_FILES + 1 or sum(i.file_size for i in infos) > MAX_UNPACKED:
                raise PackageError("Package exceeds safe resource limits")
            names = [i.filename for i in infos]
            if len(names) != len(set(names)) or any(not _safe_name(name) for name in names):
                raise PackageError("Duplicate or unsafe package paths")
            if any(stat.S_ISLNK(i.external_attr >> 16) for i in infos):
                raise PackageError("Package contains unsafe symbolic links")
            if names.count("manifest.json") != 1 or archive.getinfo("manifest.json").file_size > MAX_METADATA:
                raise PackageError("Package manifest missing or oversized")
            manifest = json.loads(archive.read("manifest.json"))
            if not isinstance(manifest, dict) or manifest.get("format") != FORMAT or manifest.get("version") != VERSION:
                raise PackageError("Unsupported Bordify package version")
            if manifest.get("launchable") is not False or manifest.get("runtime") != "macos-macho-unavailable":
                raise PackageError("Package has unexpected execution metadata")
            files = manifest.get("files")
            if not isinstance(files, dict) or set(names) != {"manifest.json", *files}:
                raise PackageError("Package manifest file list mismatch")
            for member, expected in files.items():
                if not member.startswith("payload/") or not isinstance(expected, str) or len(expected) != 64:
                    raise PackageError("Invalid package file checksum metadata")
                h = hashlib.sha256()
                with archive.open(member) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        h.update(block)
                if h.hexdigest() != expected:
                    raise PackageError("Package checksum mismatch: " + member)
            return manifest
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
        raise PackageError(f"Invalid Bordify package: {exc}") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Import macOS DMGs as Bordify .bapp archives (no macOS execution)")
    sub = parser.add_subparsers(dest="command", required=True)
    importer = sub.add_parser("import", help="Convert a DMG to a Bordify .bapp")
    importer.add_argument("source", type=Path)
    importer.add_argument("--output", type=Path)
    reader = sub.add_parser("inspect", help="Read and verify a .bapp")
    reader.add_argument("source", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "import":
            target = import_dmg(args.source, args.output)
            manifest = inspect_package(target)
            message = f"Imported {manifest['name']} as {target}. macOS Mach-O code was not converted and cannot run."
        else:
            manifest = inspect_package(args.source)
            message = (f"Bordify package: {manifest['name']} ({manifest['identifier']})\n"
                       f"Source: macOS DMG; integrity verified.\n"
                       "Status: packaged, NOT executable. Porting or a macOS compatibility runtime is required.")
        print(message)
        if os.environ.get("BORDIFY_TEST_NO_DIALOG") != "1" and shutil.which("zenity"):
            subprocess.run(["zenity", "--info", "--title=Bordify package", "--text=" + message], check=False)
        return 0
    except (PackageError, OSError) as exc:
        print("Bordify package: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
