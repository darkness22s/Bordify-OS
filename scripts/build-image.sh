#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="$ROOT/build/bordify.elf"
DEMO="$ROOT/build/apps/hello.elf"
FAULT="$ROOT/build/apps/fault.elf"
WIN32="$ROOT/build/apps/winhello.exe"
DMG="$ROOT/build/apps/dmgdemo.dmg"
ZDMG="$ROOT/build/apps/zdmg.dmg"
HFSPLUS="$ROOT/build/apps/hfsplus.dmg"
ARM="$ROOT/build/apps/armhello.mch"
DIST="$ROOT/dist"
IMG="$DIST/Bordify-OS.img"
VHD="$DIST/Bordify-OS.vhd"
MNT="$(mktemp -d)"
LOOP=""
MOUNTED=0

if [ ! -f "$KERNEL" ] || [ ! -f "$DEMO" ] || [ ! -f "$FAULT" ] || [ ! -f "$WIN32" ] || [ ! -f "$DMG" ] || [ ! -f "$ZDMG" ] || [ ! -f "$HFSPLUS" ] || [ ! -f "$ARM" ]; then
    echo "Missing kernel or demo executables; run make first." >&2
    exit 1
fi
for asset in WELCOME.TXT NOTES.TXT DOCS/README.TXT WEB/HOME.HTM WEB/GUIDE.HTM WEB/ABOUT.HTM SYSTEM/SETTINGS.CFG; do
    if [ ! -f "$ROOT/assets/fs/$asset" ]; then
        echo "Missing asset: assets/fs/$asset" >&2
        exit 1
    fi
done

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
else
    SUDO=(sudo)
fi

cleanup() {
    if [ "$MOUNTED" -eq 1 ]; then
        "${SUDO[@]}" umount "$MNT" || true
    fi
    if [ -n "$LOOP" ]; then
        "${SUDO[@]}" losetup -d "$LOOP" || true
    fi
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$DIST"
rm -f "$IMG" "$VHD" "$VHD.xz" "$IMG.xz" "$DIST/SHA256SUMS"

truncate -s 1G "$IMG"
"${SUDO[@]}" parted --script "$IMG" \
    mklabel msdos \
    mkpart primary fat16 1MiB 100% \
    set 1 boot on

LOOP="$("${SUDO[@]}" losetup --find --show --partscan "$IMG")"
"${SUDO[@]}" partprobe "$LOOP" || true
for _ in 1 2 3 4 5; do
    [ -b "${LOOP}p1" ] && break
    sleep 1
done
if [ ! -b "${LOOP}p1" ]; then
    echo "Partition device ${LOOP}p1 did not appear." >&2
    exit 1
fi

"${SUDO[@]}" mkfs.vfat -F 16 -n BORDIFY "${LOOP}p1"
"${SUDO[@]}" mount "${LOOP}p1" "$MNT"
MOUNTED=1

"${SUDO[@]}" mkdir -p "$MNT/boot/grub" "$MNT/WEB" "$MNT/DOCS" "$MNT/SYSTEM" "$MNT/APPS"
"${SUDO[@]}" cp "$ROOT/boot/grub.cfg" "$MNT/boot/grub/grub.cfg"
"${SUDO[@]}" cp "$KERNEL" "$MNT/boot/bordify.elf"
"${SUDO[@]}" cp "$DEMO" "$MNT/APPS/HELLO.ELF"
"${SUDO[@]}" cp "$FAULT" "$MNT/APPS/FAULT.ELF"
"${SUDO[@]}" cp "$WIN32" "$MNT/APPS/WINHELLO.EXE"
"${SUDO[@]}" cp "$DMG" "$MNT/APPS/DMGDEMO.DMG"
"${SUDO[@]}" cp "$ZDMG" "$MNT/APPS/ZDMG.DMG"
"${SUDO[@]}" cp "$HFSPLUS" "$MNT/APPS/HFSPLUS.DMG"
"${SUDO[@]}" cp "$ARM" "$MNT/APPS/ARMHELLO.MCH"
"${SUDO[@]}" cp -r --no-preserve=mode,ownership,timestamps "$ROOT/assets/fs/." "$MNT/"

"${SUDO[@]}" grub-install \
    --target=i386-pc \
    --boot-directory="$MNT/boot" \
    --modules="part_msdos fat multiboot2 all_video gfxterm" \
    --no-floppy \
    --recheck \
    "$LOOP"

"${SUDO[@]}" sync
"${SUDO[@]}" umount "$MNT"
MOUNTED=0
"${SUDO[@]}" losetup -d "$LOOP"
LOOP=""

qemu-img convert -f raw -O vpc "$IMG" "$VHD"
xz -T0 -9 -k "$IMG"
xz -T0 -9 -k "$VHD"

(
    cd "$DIST"
    sha256sum Bordify-OS.img Bordify-OS.vhd Bordify-OS.img.xz Bordify-OS.vhd.xz > SHA256SUMS
)

echo
echo "Created:"
ls -lh "$DIST"
