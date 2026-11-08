# Foreign application compatibility: architecture and delivery plan

Status: **native ELF32, restricted Windows PE32 console execution, read-only uncompressed UDIF disk-sector access, and a tiny ARM64 Mach-O interpreter all verified in QEMU**. Full Windows x64 installers, DMG mount/install, Apple Silicon macOS apps and Darwin/Cocoa applications are **NOT** supported.

Bordify OS currently runs a 32-bit protected-mode x86 kernel, with built-in applications linked directly into the kernel image. There is now a single-process experimental ring-3 model, a small user-memory mapping and syscall ABI, but **not** a production process scheduler, dynamic linker, 64-bit CPU mode, arbitrary-app sandbox or general-purpose Windows/macOS loader. It uses FAT16 8.3 filenames and has a fixed kernel heap. These are hard architectural constraints, not missing file associations.


## Requested five-feature acceptance status (do not relabel without real app tests)

| Requested feature | Verified now | Not yet working |
| --- | --- | --- |
| Windows x64 EXE installers | Windows **x86 PE32** demo calls three kernel32 functions | PE32+ x64 CPU execution, 64-bit DLLs, registry, installation APIs and real setup engines |
| macOS DMG mounting and installation | DMG footer + first XML blkx table, raw/zero sector reads from disk | Compressed images, whole-image filesystem mount, APFS/HFS+, PKG/.app installation |
| Apple Silicon ARM64 execution | Genuine Mach-O ARM64 demo **interprets 7 instructions** and exits with custom Bordify guest calls | General ARM64 emulation, Darwin ABI, Apple frameworks and real app workloads |
| Windows compatibility runtime | Four tiny kernel32 entry points for audited PE32 demo | Broader Win32 API, GUI, exception handling, DLLs, installation semantics |
| macOS Darwin/Cocoa runtime | Mach-O metadata recognition and synthetic ARM64 instruction demo | dyld, libSystem, Objective-C/Swift, Foundation, Cocoa/AppKit and macOS services |

**Release policy:** Only mark an item green after a real, licensed sample installer and installed application have passed automated end-to-end tests. A synthetic fixture is a lower-level milestone.

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

### Stage 0.5 — Installer container support (partial read-only UDIF sector access)

- **DMG:** The experimental UDIF reader now decodes the first XML `blkx`/base64 `mish` block map, validates raw and zero-fill chunk bounds, reads actual 512-byte sectors from a FAT16-hosted DMG **without extracting the whole image**, and explicitly refuses compressed blocks. QEMU verifies `APPS/DMGDEMO.DMG` end-to-end. **Still required:** multiple partitions/maps, zlib/LZFSE/bzip2 and other codecs, checksum validation, block-device abstraction, and HFS+/APFS filesystem mounting, plus installer workflows. A DMG is a virtual disk image, not necessarily an installer.
- **macOS PKG:** Implement a sandboxed XAR reader, compression and package manifest parsing. Installation scripts and dependency management must be handled explicitly, with clear trust/permission policy.
- **macOS APP:** Support long filenames, symlinks, bundle `Info.plist`, architecture selection and code-signature validation policy before passing executable to an isolated Mach-O process runtime.
- **Windows x64 EXE installer:** Beyond PE32+ recognition, needs long-mode/64-bit processes, 64-bit Windows APIs, dynamic DLL loading, registry semantics, installer file/shortcut operations and a target app runtime. Self-extractors such as NSIS/Inno/InstallShield are different formats layered atop PE; compatibility must be tested individually.
- **MSI:** OLE/CFB signature alone is not proof of a Windows Installer database; use the MSI directory/stream structures to validate and install under a Windows Installer-compatible runtime.
- **Hardware:** Plan a x86-64 kernel and 64-bit boot path while keeping the current 32-bit boot image working until a tested replacement exists.

### Stage 1 — Native process infrastructure (first executable milestone)

**Implemented in this PR:** 32-bit x86 GDT user selectors and a TSS kernel stack, a basic supervisor-only identity paging arrangement with one 256-KiB user mapping, `int 0x80` serial-write and exit syscalls, ELF32 PT_LOAD parsing and mapping with range checks, a demo user program read from FAT16, and a deliberate ring-3 protection-fault test. CI requires the demo and fault messages to appear on the QEMU serial port and the desktop to start afterward.

The Files app now has an Apps sidebar shortcut. Double-click `APPS/HELLO.ELF` to actually execute this **native Bordify ABI** program; it is unrelated to Wine or macOS application support. `APPS/FAULT.ELF` demonstrates user-fault containment. This proof is **not production-secure**: one user address range, one process at a time, no scheduler, no user-level resource handles, no general ASLR, no W^X/NX enforcement, no proper ELF relocation/dynamic loading, and a simple fixed 32 MiB kernel heap. The currently present mapped upper identity ranges should be replaced with tighter kernel mappings. It is not suitable for arbitrary untrusted programs yet.

**Remaining work:**


- Replace the shared, single-user mapping with private per-process address spaces, page-table management, guarded stacks and tighter kernel/user mappings.
- Add process scheduling, syscall validation, independent file handles and resource lifecycle, with capability-scoped system services.
- Extend the current ELF32 hello-world ABI to support more than serial output and exit.
- Add a VFS preserving long filenames, file permissions and metadata; FAT16 can remain boot storage.
- Expand QEMU tests to exercise concurrent processes, privilege boundaries, malformed ELF and resource exhaustion.

### Stage 1.4 — Bounded ARM64 Mach-O execution proof of concept (in this PR)

A standalone, synthetic **Apple Silicon ARM64 Mach-O** with a genuine `MH_MAGIC_64`, `CPU_TYPE_ARM64`, `LC_SEGMENT_64` and `LC_MAIN` is loaded and interpreted on an **x86** Bordify kernel. The test-only interpreter supports only the ARM64 instructions `ADR`, `MOVZ`, and `SVC #0`; `SVC` uses a **custom Bordify test ABI**, not Darwin or iOS syscalls. It runs within an explicitly bounded 64-KiB guest byte array and a fixed 1,024-instruction budget. `APPS/ARMHELLO.MCH` prints a message and exits through the guest ABI; CI confirms its output while Bordify continues to the desktop.

**This is NOT native Apple Silicon app compatibility.** Typical macOS ARM64 code depends on thousands of other instructions and full Darwin, dyld, Objective-C/Swift and Cocoa frameworks, signatures and OS services. It is a safe interpreter proof of concept that exercises real ARM64 instruction encoding.

### Stage 1.5 — Restricted real Win32 PE32 console execution (in this PR)

Implemented a bounded PE32 loader for a single x86 executable at the fixed user base `0x40000000`, with checked sections, import names and a four-function `kernel32.dll` bridge implemented as ring-3 stdcall trampolines: `ExitProcess`, `GetStdHandle`, `WriteFile`, and `GetLastError`. The independently generated `APPS/WINHELLO.EXE` is a real PE32 executable with a proper import table; it calls `GetStdHandle`, `WriteFile`, and `ExitProcess`. The VM boot test requires its console output and exit status, and it can be launched from the Files Apps sidebar. Unsupported machine types, invalid headers, unknown imports, relocation-dependent images and non-console executables are deliberately refused. This is **not** general Windows compatibility; it supports an intentionally tiny console-only API subset and must not be used to run untrusted installers. No x64 / PE32+ execution is implemented.

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
5. QEMU serial confirms ring-3 ELF32 sample execution, isolated supervisor-memory page fault and successful return to the desktop. Further execution must build on this native process ABI; no direct PE or Mach-O kernel execution.
