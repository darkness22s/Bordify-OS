# Bordify OS — Linux edition

This is a **new Linux boot path**, not an in-place replacement for the legacy
32-bit custom Bordify kernel. The Linux ISO uses Debian's Linux kernel and
Debian live-build. It starts the Bordify SDL2 desktop in an X11/Openbox session.

## What is preserved

The Linux binary compiles Bordify's existing kernel/desktop.c, kernel/gui.c,
kernel/gfx.c, kernel/icons.c, kernel/ui.c, kernel/settings.c, bitmap font
generator, and theme definitions directly. Wallpaper, three shortcuts, dock,
font rendering, window borders and interaction logic are the original code.
The Linux-backed Files, web and Settings window **contents** are implemented
separately for Linux, because the legacy apps depend on FAT16/PS2/ATA and
the original freestanding kernel. Files opens the Linux file manager,
web opens the system browser, and Settings modifies the original theme and
wallpaper, with preferences stored in ~/.config/bordify/settings.cfg.

This is a separate edition: existing FAT16 image, custom process loader,
kernel image and experiments remain available from the original Makefile.

## Running on an existing Linux installation

Dependencies (Debian/Ubuntu package names):

    sudo apt install build-essential libsdl2-dev python3-pil fonts-dejavu-core \
        xdg-utils zenity openbox pcmanfm wine wine64 7zip
    make -C linux
    python3 -m unittest discover -s linux/tests -p 'test_*.py'
    sudo install -m755 build/linux/bordify-shell /usr/local/bin/bordify-shell
    sudo install -m755 linux/bordify_open.py /usr/local/bin/bordify-open
    sudo install -m755 linux/bordify_package.py /usr/local/bin/bordify-package
    sudo install -m755 linux/choose_app.py /usr/local/bin/bordify-choose-app
    bordify-shell

Run within an X11 session; the standalone binary works in a normal window
manager. Linux graphics use SDL2, not a direct hardware framebuffer. The
screen resolution follows the display in normal mode. Native Linux windows
render over the Bordify backdrop via the host window manager.

## Build a bootable Linux ISO

On a trusted Debian-compatible build machine with live-build, sudo and the
host build dependencies installed:

    sudo apt install live-build debootstrap squashfs-tools xorriso \
        grub-pc-bin grub-efi-amd64-bin
    bash scripts/build-live-iso.sh

Output: dist/Bordify-OS-Linux-amd64.iso (+ SHA256 file). The 64-bit ISO is a
Debian Live hybrid image intended to boot on compatible x86-64 BIOS/UEFI
machines and VM firmware. The live user's session starts Bordify after X11
login. No installer, persistence, Secure Boot signing, or ARM64 image is
provided. Building the ISO needs network access to Debian package mirrors.

Suggested QEMU boot test:

    qemu-system-x86_64 -m 4096 -enable-kvm \
        -cdrom dist/Bordify-OS-Linux-amd64.iso -boot d

If KVM is unavailable, omit -enable-kvm. Use at least 4 GiB RAM when
running modern GUI applications.

## Windows apps

Open a .exe/.msi via bordify-open, drag its path onto the Bordify backdrop,
or choose it with the Files window. A valid .exe is dispatched to Wine;
MSI uses wine msiexec /i. The Wine prefix is
~/.local/share/bordify/wineprefix (NOT a security sandbox).
The live ISO includes Debian's Wine and wine64 packages. Old 32-bit programs
may need 32-bit Wine multiarch libraries. For Debian, on an installed
writable system:

    sudo dpkg --add-architecture i386
    sudo apt update
    sudo apt install wine32:i386

Wine app compatibility varies; anti-cheat, some drivers, newer Windows
APIs and many installers may fail. Proton is not a drop-in replacement
for the system Wine launcher; a game manager can integrate Proton later.

## macOS DMG imports as Bordify .bapp

Bordify **no longer tries to launch macOS .app / .pkg / Mach-O programs
through Darling**. Instead, opening a UDIF .dmg file starts the native
Bordify importer, which uses Debian's 7-Zip to extract a supported DMG and
looks for one valid .app bundle. It creates a Bordify-specific **.bapp v1**
ZIP container with a JSON manifest, the original bundle files, SHA-256
digests, source-image fingerprint, and explicit non-executable status.
Bordify recognizes and checks .bapp files without invoking a macOS runtime.

On a Linux development machine, install the Debian package 7zip and run:

    python3 linux/bordify_package.py import ~/Downloads/Example.dmg
    python3 linux/bordify_package.py inspect ~/.local/share/bordify/apps/Example-*.bapp

Or open the DMG from **Files → Import DMG...**. Converted packages are
stored under ~/.local/share/bordify/apps by default, named with the source
SHA-256 prefix. To choose a path explicitly:

    python3 linux/bordify_package.py import Example.dmg --output ./Example.bapp

Open a .bapp with bordify-open to verify its checksums and view its metadata.
The format is versioned (manifest format org.bordify.bapp, version 1) and
does not contain an executable launch recipe for macOS binaries.

**This is an importer, not a macOS-to-Linux executable converter.**
Mach-O code and its dependencies are packaged, **not translated or run**.
To launch such a program as a native Bordify app, it must be ported or
reimplemented for Linux (or an independent macOS ABI/runtime integrated).
Changing DMG bytes into .bapp does not make Apple frameworks, Swift,
Objective-C, dyld or AppKit work on Linux.

**Current import limitations:** 7-Zip must support the DMG's format,
encryption is unsupported, archives containing symbolic links/hard links
are rejected, and imports currently require exactly one .app bundle with
an executable Mach-O file. Many real DMGs include an /Applications
symlink and will currently be rejected. Neither arbitrary DMGs nor
Chrome for macOS are certified to import. The importer verifies archive
paths, file count, size bounds and digests but **is not a sandbox**;
untrusted images should be processed in a disposable VM.


## Safety / verification

The launcher validates basic PE signatures and the .bapp reader checks
ZIP paths and per-file SHA-256 digests. These checks are NOT malware detection.
Launching foreign code may run untrusted code with your user privileges.
Do not execute unknown installers; use a disposable VM for untrusted files.

Host CI tests: native SDL2 compilation + 1024x768 screenshot assertions,
Python launcher dispatch/error-path unit tests and a real MinGW-built
64-bit Windows PE console program run under Wine/Xvfb. The optional ISO
job verifies image assembly; full real-hardware, Darling or GUI Chrome
compatibility requires separate acceptance runs.

To revert, simply keep using the legacy root Makefile and original image.
