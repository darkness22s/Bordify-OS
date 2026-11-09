#!/usr/bin/env bash
# Download a real third-party Windows installer, launch in Wine and verify
# actual 7z compression/extraction. Never commit the downloaded executable.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/linux/third-party"
cd "$ROOT/build/linux/third-party"

URL="https://github.com/ip7z/7zip/releases/download/26.06/7z2606-x64.exe"
curl --fail --location --retry 3 --connect-timeout 20 "$URL" --output 7zip-installer.exe
python3 - <<'PY'
from pathlib import Path
import struct
p = Path("7zip-installer.exe")
data = p.read_bytes()
assert len(data) > 500_000, "7-Zip installer too small"
assert data[:2] == b"MZ", "not a Windows PE executable"
ptr = struct.unpack_from("<I", data, 0x3C)[0]
assert ptr + 4 < len(data) and data[ptr:ptr+4] == b"PE\0\0"
print("Official 7-Zip Windows executable: real PE verified", len(data))
PY
sha256sum 7zip-installer.exe

export WINEPREFIX="$PWD/wineprefix"
export WINEDEBUG=-all
mkdir -p "$WINEPREFIX"

# All foreign code runs inside the ephemeral GitHub runner, NOT against a user's files.
timeout 180s xvfb-run -a wine 7zip-installer.exe /S > 7zip-install.log 2>&1 || {
    cat 7zip-install.log
    echo "7-Zip Windows installer failed under Wine" >&2
    exit 1
}
SEVEN="$WINEPREFIX/drive_c/Program Files/7-Zip/7z.exe"
if [ ! -f "$SEVEN" ]; then
    find "$WINEPREFIX/drive_c" -iname "7z.exe" -print
    echo "7-Zip did not install the Windows 7z.exe program" >&2
    exit 1
fi
printf 'BORDIFY_REAL_THIRD_PARTY_WINDOWS_APP_OK\n' > sample.txt
timeout 90s xvfb-run -a wine "$SEVEN" a sample.7z sample.txt > 7zip-archive.log 2>&1
mkdir -p unpacked
timeout 90s xvfb-run -a wine "$SEVEN" x sample.7z -ounpacked -y > 7zip-extract.log 2>&1
cmp sample.txt unpacked/sample.txt
echo "Genuine 7-Zip Windows EXE: installed and compressed/extracted real data under Wine: PASS"
