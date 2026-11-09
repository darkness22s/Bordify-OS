#!/usr/bin/env bash
# Builds a real Debian Linux hybrid live ISO; use on a trusted builder with sudo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
command -v lb >/dev/null || { echo "Install live-build first" >&2; exit 1; }
make -C "$ROOT/linux"
WORK="$ROOT/build/linux/live-build"
mkdir -p "$WORK" "$ROOT/dist"
cd "$WORK"
# Work directory is isolated from the legacy kernel image builder.
# Use Debian 13 live-build to install and stage the amd64 kernel into /live.
# Disabling linux-packages here left the ISO without vmlinuz/initrd.
lb config --mode debian --distribution trixie --architectures amd64 \
  --binary-images iso-hybrid --archive-areas "main contrib non-free-firmware" \
  --debian-installer none --initsystem systemd --security false --linux-packages linux-image --linux-flavours amd64 --bootappend-live "boot=live components" \
  --iso-application "Bordify OS Linux Edition" \
  --iso-publisher "Bordify OS"
mkdir -p config/archives config/package-lists \
  config/includes.chroot/usr/local/bin \
  config/includes.chroot/usr/share/xsessions \
  config/includes.chroot/usr/share/applications \
  config/includes.chroot/etc/lightdm/lightdm.conf.d
# Ubuntu's older live-build defaults to obsolete trixie/updates security paths.
# Use Debian's current security suite explicitly for the live rootfs and image.
printf '%s\n' 'deb http://security.debian.org/debian-security trixie-security main contrib non-free-firmware' \
  > config/archives/debian-security.list.chroot
cp config/archives/debian-security.list.chroot config/archives/debian-security.list.binary
cp "$ROOT/linux/live/packages.list.chroot" config/package-lists/bordify.list.chroot
install -m 755 "$ROOT/build/linux/bordify-shell" config/includes.chroot/usr/local/bin/bordify-shell
install -m 755 "$ROOT/linux/bordify_open.py" config/includes.chroot/usr/local/bin/bordify-open
install -m 755 "$ROOT/linux/choose_app.py" config/includes.chroot/usr/local/bin/bordify-choose-app
install -m 755 "$ROOT/linux/live/session.sh" config/includes.chroot/usr/local/bin/bordify-session
install -m 644 "$ROOT/linux/live/xsession.desktop" config/includes.chroot/usr/share/xsessions/bordify.desktop
install -m 644 "$ROOT/linux/live/bordify-app.desktop" config/includes.chroot/usr/share/applications/bordify-app.desktop
install -m 644 "$ROOT/linux/live/lightdm.conf" config/includes.chroot/etc/lightdm/lightdm.conf.d/50-bordify.conf
sudo lb build
ISO="$(find . -maxdepth 1 -name 'live-image-*.hybrid.iso' -print -quit)"
[ -n "$ISO" ] || { echo "Live-build did not produce an ISO" >&2; exit 1; }
cp "$ISO" "$ROOT/dist/Bordify-OS-Linux-amd64.iso"
sha256sum "$ROOT/dist/Bordify-OS-Linux-amd64.iso" > "$ROOT/dist/Bordify-OS-Linux-amd64.iso.sha256"
echo "Wrote $ROOT/dist/Bordify-OS-Linux-amd64.iso"
