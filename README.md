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
(`APPS/WINHELLO.EXE`) and a constrained Win32 disk-I/O executable
(`APPS/WINIO.EXE`) using seven `kernel32.dll` bridge functions,
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

**Darwin/Cocoa Objective-C lifecycle test gate (green):**
`APPS/COCOA.MCH` is a real Clang-assembled ARM64 Mach-O that imports
`_objc_getClass`, `_sel_registerName`, and `_objc_msgSend` as unresolved
symbols. A new **restricted static Mach-O binder** validates its symbol
table and ARM64 branch relocations, then constructs a checked opt-in
Bordify `LC_NOTE` binding table. The guest executes its regular ARM64
`BL` calls using the Objective-C `x0/x1/x2` ABI; the interpreter
dispatches only the three allowed symbols.

The runtime models `NSString` string creation/UTF-8/length,
`NSWindow` creation/title/visibility, and `NSApplication`
shared application/start/running/terminate lifecycle. The sample
performs **25 linked Objective-C calls**, prints its NSString through
Darwin-style `write`, and exits zero. Unit tests reject malformed
imports, unknown classes, bad object receivers, and infinite guest
loops; GitHub Actions verifies the behavior in x86 QEMU, including
desktop startup. Open **Files → Apps → COCOA.MCH** to repeat the demo.

**This is NOT genuine Cocoa GUI support.** The modeled window does
not render on screen, the `run` method is nonblocking, and neither
Apple dyld, libobjc, Swift nor real Foundation/AppKit is implemented.
Arbitrary macOS applications and bundles still do not run. Green
applies **only to the audited ObjC ABI lifecycle test**.

**Windows compatibility runtime: native Win32 file I/O test gate (green):**
`APPS/WINIO.EXE` is a real x86 PE32 program running as a ring-3 user
process with seven recognized `kernel32.dll` imports: `ExitProcess`,
`GetStdHandle`, `WriteFile`, `GetLastError`, `CreateFileA`,
`ReadFile`, and `CloseHandle`.

It uses Windows stdcall API stubs to create the strictly allowed
`C:\\BORDIFY\\WIN32.TXT` destination, writes a message through
`WriteFile`, closes the handle to commit the data to Bordify FAT16,
reopens and reads the file, compares bytes inside ring 3, then prints
a success message and exits. Host tests reject bad file names, invalid
handles, invalid user memory and oversized writes. QEMU CI extracts
`APPS/WIN32.TXT` from the *persistent* disk image and checks its
contents byte-for-byte. Run it through **Files → Apps → WINIO.EXE**.

This green status is for **one audited native Win32 file-I/O workflow**,
not for Windows desktop compatibility in general. The user-mode
runtime still lacks arbitrary Windows programs, DLL loading, GUI,
threading, registry, general NT filesystem/handle semantics and
production-strength process sandboxing.

**Apple Silicon execution: compiled ARM64 Mach-O workload test gate (green):**
Bordify now interprets more AArch64 instructions, including 32/64-bit
integer arithmetic, NZCV flags, conditional branching, BL/RET calls,
bounded stack loads/stores and conditional select. The Apple-targeted
Clang toolchain builds two genuinely compiled/assembled Mach-O test
executables, wrapped with a checked single-segment, no-relocations
loader:

- `APPS/ARMWORK.MCH`: independently assembled ARM64 program. Executes
  **55 guest instructions** (arithmetic loop, stack, call/return and
  Darwin BSD output syscall) and exits successfully.
- `APPS/ARMCALC.MCH`: independently **C-compiled** ARM64 program.
  Executes **70 guest instructions** to calculate/check a sum and exits
  successfully. A negative test deliberately changes the expected sum
  and verifies a nonzero exit.

These workloads pass GitHub Actions unit and x86-QEMU boot checks.
They run through **interpreter-based CPU emulation**, not on native ARM
hardware. Only these self-contained programs are verified. **General
Apple Silicon macOS apps cannot run** without a substantially more
complete ARM64 instruction set, Mach-O loader, Darwin runtime,
frameworks, and isolated process model.

**Experimental x64 installer flow (audited fixture only):**
Open `APPS/SETUP64.EXE` in Files. This genuine PE32+ AMD64 *test*
installer runs through the bounded x64 interpreter, calls
`CreateFileA`, `WriteFile` and `CloseHandle`, and installs its embedded
PE32+ application to `APPS/APP64.EXE`. Bordify verifies the installed
file and executes it; `APP64.EXE` can be relaunched in Files.
GitHub Actions boots QEMU with a writable copy of the disk and then
extracts the installed file for byte-for-byte verification.

**This proves one end-to-end installer pathway, NOT compatibility with
arbitrary Windows x64 setup EXEs** (MSI, Inno, NSIS, InstallShield,
Visual C++ installers, DLL-based setup, graphical installers or real
third-party apps). The installer is a generated regression fixture,
not an external Windows application, and is restricted to one output
filename. The kernel cannot safely run untrusted installers yet.

**Read-only compressed macOS DMG extraction and a constrained installer test:**
`APPS/MOUNT.DMG` is a generated zlib-compressed UDIF image with a
minimal HFS+ catalog containing an ARM64 Mach-O file, `DARWIN.MCH`.
Opening this specific fixture in Files uses the new bounded HFS+
read-only mount, catalog-record search and data-fork reader, extracts
the file to `APPS/DMGAPP.MCH` on FAT16, and runs the installed
program through the experimental ARM64 Darwin interpreter. CI extracts
the saved file from QEMU's disk and compares it byte-for-byte against
the bundled Mach-O; malformed HFS+ records and file extents are rejected.

**Only this built-in HFS+ DMG file-extraction workflow is fully tested.**
It does **not** yet mount arbitrary real-world macOS DMGs, process
APFS, follow full HFS+ B-trees and overflow extents, install macOS
`.app` bundles or `.pkg` scripts, or provide full Darwin/Cocoa.

The development branch also probes **HFS+/HFSX volume and catalog B-tree headers**
inside compressed DMGs. `APPS/HFSPLUS.DMG` remains a header-only
fixture, whereas `APPS/MOUNT.DMG` exercises the newer restricted
read-only HFS+ catalog and file extraction path. Neither is a
general-purpose real-world macOS installation image.

This **does not** run general third-party Windows x64 installers or arbitrary .exe files,
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
