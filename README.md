# Bordify OS

Bordify is a small, real 32-bit x86 operating system. It boots through
Multiboot2 and GRUB into its own kernel, draws a windowed desktop on a
linear framebuffer, and reads and writes files on a real FAT16 partition.
There is no Linux, browser host or web-app wrapper underneath.

![build](https://github.com/darkness22s/Bordify-OS/actions/workflows/build.yml/badge.svg)

## What is inside

- 32-bit protected-mode kernel written in C and assembly
- GRUB Multiboot2 boot path
- 1024 x 768, 32-bit linear framebuffer
- Own heap, strings, formatting, GDT, IDT and interrupt handling
- PS/2 keyboard and mouse, PIT clock and CMOS real-time clock
- ATA PIO storage with a read/write FAT16 filesystem
- Custom window manager, buttons, text fields, dialogs, tabs and scrolling
- Anti-aliased DejaVu bitmap fonts baked into the kernel
- Three apps: Files, web and Settings

The initial desktop has the three app shortcuts in the top-left corner
and a rounded dock along the bottom edge.

## Apps

**Files** is a working file manager. Browse the disk, open folders, create
folders and text files, rename items, read documents, and delete files.
Every change is written to the FAT16 disk image.

**web** is a native HTML reader for the documents in `WEB/` and for HTTP/HTTPS pages on the network. It parses the installed pages, renders formatted
text, and follows clickable links. It does not use a host browser. The
network stack talks to an Intel e1000 adapter and uses the QEMU/VirtualBox
user-mode NAT address range. HTTP and HTTPS (TLS 1.2) are supported.

**Settings** changes the theme, wallpaper and accent, and writes them to
`SYSTEM/SETTINGS.CFG`. It also shows kernel, memory, display, disk, clock
and uptime information, plus Restart and Power off controls.

## Experimental cross-platform compatibility (development branch only)

The draft compatibility branch includes a real isolated ELF32 demo
(`APPS/HELLO.ELF`), a tiny Windows PE32 console executable
(`APPS/WINHELLO.EXE`) using four limited `kernel32.dll` bridges,
a small **interpreted** Apple Silicon Mach-O demonstration
(`APPS/ARMHELLO.MCH`), and read-only raw/zero and bounded zlib-compressed UDIF sector
access using synthetic disk images (`APPS/DMGDEMO.DMG`, `APPS/ZDMG.DMG`).
These demonstrations are checked by
QEMU CI. The original ARM64 guest demo uses a **custom test ABI**, not Darwin.

The experimental branch now also includes **two 64-bit compatibility proofs**:
- `APPS/WIN64.EXE`: a genuine x86-64 PE32+ console image, interpreted
  (not executed natively) with a bounded AMD64 instruction subset and
  Microsoft x64 ABI bridges for `GetStdHandle`, `WriteFile`, `ExitProcess`.
- `APPS/DARWIN.MCH`: a genuine ARM64 Mach-O test using a subset of
  Darwin-style BSD syscalls and a test-only Objective-C bridge handling
  `NSString` and `NSApplication` methods, not actual dyld/libobjc/Cocoa.

Both demos have host-side tests and QEMU boot checks. They can also be
launched from the Files app's Apps directory.

The development branch also probes **HFS+/HFSX volume and catalog B-tree headers** inside
compressed DMGs, including catalog-extent metadata, without mounting
or reading an actual HFS+ filesystem. `APPS/HFSPLUS.DMG` is a synthetic
header fixture (including a synthetic B-tree header), not a usable macOS installation image.

This **does not** run real Windows x64 installers, arbitrary .exe files,
mount or install real macOS DMGs, execute general Apple Silicon applications,
or implement full Darwin/Cocoa frameworks. Untrusted applications must not be launched by this
early-stage kernel. [Details and compatibility gates](docs/application-compatibility.md).

## Download and run

Prebuilt images are attached to the [latest release](https://github.com/darkness22s/Bordify-OS/releases/latest):

- `Bordify-OS.img` - raw MBR disk image
- `Bordify-OS.img.xz` - compressed raw image
- `Bordify-OS.vhd` - VirtualBox/Hyper-V virtual disk
- `Bordify-OS.vhd.xz` - compressed VHD
- `SHA256SUMS` - checksums

Every push to `main` also produces the same files as the
`Bordify-OS-images` artifact in the **Actions** tab. Disk images are 1 GiB by
default; QEMU's `-m 128` setting below controls RAM separately from disk capacity.

Run the raw image with QEMU:

```sh
qemu-system-i386 \
  -drive file=Bordify-OS.img,format=raw,if=ide \
  -nic user,model=e1000 \
  -m 128 \
  -vga std
```

The e1000 adapter uses the user-mode NAT network: address `10.0.2.15`,
gateway `10.0.2.2` and DNS `10.0.2.3`. For VirtualBox, create a 32-bit
"Other/Unknown" VM with 128 MB of RAM, attach `Bordify-OS.vhd` to an IDE
controller, keep the default NAT adapter, and start it.

## Controls

- Click a desktop shortcut to open an app.
- Drag a window by its title bar.
- Double-click a folder to enter it and a text file to read it.
- Select an item and use **Rename** or **Delete** in the Files toolbar.
- `F1`, `F2` and `F3` open Files, web and Settings from the desktop.
- `Esc` closes dialogs, editors and prompts.

## Building

CI is the canonical build path. The image workflow installs the 32-bit GCC
toolchain, NASM, GRUB, dosfstools, parted and QEMU image tools, compiles
the kernel, creates the FAT16 filesystem, installs GRUB, and uploads the
disk images. On the experimental compatibility branch, CI also boots the OS in QEMU,
runs native ELF32, a restricted Windows PE32 console demo and an interpreted
ARM64 Mach-O demo, and checks uncompressed DMG sector reads.

The build is split into two commands:

```sh
make -j"$(nproc)"
sudo bash scripts/build-image.sh
```

## Layout

```
boot/        Multiboot2 entry, linker script and GRUB config
kernel/      kernel, drivers, filesystem, graphics and applications
assets/fs/   seed files copied onto the FAT16 partition
tools/       bitmap font generator
scripts/     disk image assembly
.github/     public CI build
```

## License

MIT. See [LICENSE](LICENSE).
