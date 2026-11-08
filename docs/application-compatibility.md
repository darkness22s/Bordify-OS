# Foreign application compatibility: architecture and delivery plan

Status: **executable + installer-container inspection implemented; installing and launching foreign apps not implemented**.

Bordify OS currently runs a 32-bit protected-mode x86 kernel, with built-in applications linked directly into the kernel image. There is no user-process model, virtual address-space isolation, syscall ABI, dynamic linker, 64-bit CPU mode, or general-purpose executable loader. It uses FAT16 8.3 filenames and has a fixed kernel heap. These are hard architectural constraints, not missing file associations.

## End-state goals

- Launch a useful, clearly defined subset of **Windows x86 applications** using a PE32 loader and a clean-room Win32-compatible runtime (or a Wine-derived implementation if the required POSIX and other dependencies can be ported).
- Launch a useful subset of **macOS applications** after implementing Mach-O loading and sufficient Darwin, Objective-C, Foundation, and Cocoa/AppKit compatibility. macOS software is not equivalent to Mach-O format support; compatibility will be app-specific and Apple software licensing/redistribution must be respected.
- Support **64-bit Intel and ARM** applications after a deliberate 64-bit migration, or through CPU emulation / binary translation where practical. Apple's modern Mac applications generally target x86-64 or ARM64 and often depend on extensive frameworks.
- Revisit **Android** via a substantial hosted Android runtime (Linux-kernel compatibility, drivers, or a guest VM/emulator). A full Android emulator is not feasible in the present 32-bit 128 MiB QEMU configuration.
- Treat **iPhone simulators** as a separate, much harder effort requiring a compatible mobile OS/framework environment; Apple's simulator components have platform and licensing restrictions. Do not imply that a Mach-O loader can run iOS apps.

## Implementation sequence

### Stage 0 — Safe discovery (this PR)

1. Parse DOS MZ, Windows PE32/PE32+, Apple thin/fat Mach-O, and ELF32/ELF64 signatures with bounds checks.
2. Show format, architecture, and diagnostics when opening files in Files. Do not cast foreign executable bytes to function pointers.
3. Add host-side test fixtures and run them in CI on pull requests.
4. Explicitly report incompatible/malformed files rather than claiming they can run.
5. Inspect Apple DMG UDIF trailing `koly` metadata using a 512-byte random-access FAT16 read (rather than allocating the image); identify Apple XAR `.pkg` packages and OLE containers that **might** be Windows MSI files.
6. Confirm x86-64 Windows PE32+ and ARM64 Apple Mach-O recognition with synthetic test fixtures.

Notes: executable/container prefix inspection examines only the bytes File Manager reads (currently at most about 14 KiB), while DMG metadata reads the last 512 bytes of the file. It is **not** a complete image verifier or loader. The FAT16 filesystem cannot currently store a regular macOS `.app` bundle with long paths and POSIX metadata. Some signatures and architectures are recognized heuristically; execution requires full format/segment/import validation.

### Stage 0.5 — Installer container support (not yet implemented)

- **DMG:** Fully implement UDIF `blkx` chunk maps, support needed compression algorithms, validate checksums and create a read-only block-device abstraction. Add HFS+ and APFS readers. A DMG is a virtual disk image, not itself a macOS executable or automatic installer.
- **macOS PKG:** Implement a sandboxed XAR reader, compression and package manifest parsing. Installation scripts and dependency management must be handled explicitly, with clear trust/permission policy.
- **macOS APP:** Support long filenames, symlinks, bundle `Info.plist`, architecture selection and code-signature validation policy before passing executable to an isolated Mach-O process runtime.
- **Windows x64 EXE installer:** Beyond PE32+ recognition, needs long-mode/64-bit processes, 64-bit Windows APIs, dynamic DLL loading, registry semantics, installer file/shortcut operations and a target app runtime. Self-extractors such as NSIS/Inno/InstallShield are different formats layered atop PE; compatibility must be tested individually.
- **MSI:** OLE/CFB signature alone is not proof of a Windows Installer database; use the MSI directory/stream structures to validate and install under a Windows Installer-compatible runtime.
- **Hardware:** Plan a x86-64 kernel and 64-bit boot path while keeping the current 32-bit boot image working until a tested replacement exists.

### Stage 1 — Native process infrastructure (required before foreign apps)

- Design private per-process virtual address spaces, page-table management, user-mode (ring 3), guarded stacks and kernel/user memory boundaries.
- Add a syscall entry mechanism, file handles, bounded copies from userspace, fault isolation, an exit/reap mechanism, and preemptive/cooperative scheduling as chosen.
- Build an **ELF32 native Bordify ABI** first and run a minimal hello-world user program as a smoke test. Keep drivers and GUI in trusted components.
- Add a VFS that preserves long filenames and file metadata. FAT16 may remain boot storage but should not be the only application filesystem.
- Boot-test a tiny process in QEMU and assert serial output, exit status, and fault containment.

### Stage 2 — Target Windows x86 applications

- Implement robust PE32 mapping with address checks, sections, relocations, imports, TLS/callback policies, resources, and a loader error model.
- Build tested, versioned implementations of the Win32 APIs that specific sample apps require (e.g., a small console app first, then GDI windows/controls, files, threads, and synchronization).
- Distinguish Win32 console vs GUI programs and x86 vs x86-64; unsupported imports must result in clean errors.
- Test with legally redistributable sample binaries. Run unknown/untrusted PE code only in isolated user address spaces.

### Stage 3 — macOS, 64-bit, and language runtime capabilities

- Plan the 64-bit kernel transition, higher-memory mapping, ABI changes, CPU feature detection and graphics/multitasking requirements.
- Parse/load Mach-O segments, dyld-style linking and symbol binding, but only inside isolated processes.
- Investigate Darwin syscall, libSystem, Objective-C runtime, Foundation and AppKit bridges. The existing GUI is *not* Cocoa.
- Start with a minimal, independently built Mach-O command-line test. GUI apps require significant framework work.
- Prefer a carefully evaluated compatibility project if portable and license-compliant; no off-the-shelf macOS framework layer drops directly into a freestanding 32-bit kernel.

### Stage 4 — Mobile experiments

- Measure available hardware virtualization, CPU architecture, memory, MMU, disk and GPU resources first.
- Android may be practical as a **remote or hosted** guest before native emulation. Treat iOS simulation separately with its platform and licensing limitations.
- Document performance and compatibility by app, never advertise universal mobile compatibility.

## Engineering rules

- Keep compatibility runtimes outside the privileged kernel wherever possible.
- No execution of untrusted bytes inside the kernel; no unsigned native plugins.
- Bound every header read and multiplication; require complete headers before mapping.
- Include host unit tests, QEMU boot tests, and negative fuzz cases for file parsers and loaders.
- Do not promise that inspecting a PE or Mach-O file implies that Bordify can run it.

## First acceptance gates after this PR

1. Bordify compiles with `kernel/exec_inspect.c` included in the kernel.
2. Host tests pass via `make test` in GitHub Actions.
3. Double-clicking a known PE/Mach-O file in Files shows a truthful format/architecture/unsupported notice, without crashing.
4. A deliberately truncated binary or invalid DMG metadata is reported as invalid/incomplete.
5. Subsequent work starts with a protected user-process proof of concept, not direct execution of PE or Mach-O in kernel mode.
