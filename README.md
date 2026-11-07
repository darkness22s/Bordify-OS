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

**web** is a native HTML reader for the documents in `WEB/`. It parses the
installed pages, renders formatted text, and follows clickable links. It
does not use a host browser. It is intentionally local: the current build
does not include an Ethernet or TCP/IP stack, and HTTPS is not available.

**Settings** changes the theme, wallpaper and accent, and writes them to
`SYSTEM/SETTINGS.CFG`. It also shows kernel, memory, display, disk, clock
and uptime information, plus Restart and Power off controls.

## Download and run

Builds are produced by GitHub Actions. Open the **Actions** tab, choose the
latest successful **Build Bordify OS images** run, and download the
`Bordify-OS-images` artifact. It contains:

- `Bordify-OS.img` - raw MBR disk image
- `Bordify-OS.img.xz` - compressed raw image
- `Bordify-OS.vhd` - VirtualBox/Hyper-V virtual disk
- `Bordify-OS.vhd.xz` - compressed VHD
- `SHA256SUMS` - checksums

Run the raw image with QEMU:

```sh
qemu-system-i386 \
  -drive file=Bordify-OS.img,format=raw,if=ide \
  -m 128 \
  -vga std
```

For VirtualBox, create a 32-bit "Other/Unknown" VM with 128 MB of RAM,
attach `Bordify-OS.vhd` to an IDE controller, and start it.

## Controls

- Click a desktop shortcut to open an app.
- Drag a window by its title bar.
- Double-click a folder to enter it and a text file to read it.
- `F1`, `F2` and `F3` open Files, web and Settings from the desktop.
- `Esc` closes dialogs, editors and prompts.

## Building

CI is the canonical build path. The image workflow installs the 32-bit GCC
toolchain, NASM, GRUB, dosfstools, parted and QEMU image tools, compiles
the kernel, creates the FAT16 filesystem, installs GRUB, and uploads the
disk images. The pipeline compiles and packages only; it does not boot or
run the operating system in CI.

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
