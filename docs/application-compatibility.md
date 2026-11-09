# Foreign application compatibility: architecture and delivery plan

Status: **five audited experimental CI milestones pass:** native Win32 PE32 file-I/O runtime; restricted PE32+ x64 installer; zlib-compressed HFS+ DMG extraction/installation; Clang-generated Apple ARM64 execution; and statically linked Objective-C ABI plus modeled Cocoa lifecycle. **General third-party Windows/macOS apps and actual Cocoa GUI frameworks are not supported.** A generated HFS+ DMG can deliver an ARM64 Mach-O program which Bordify launches and persists to disk. **Arbitrary third-party DMGs, APFS, PKG/.app installation, general macOS compatibility and full Darwin/Cocoa are NOT supported.**

## November 2026 Windows runtime development update (draft PR #2)

The development branch now contains **independently tested, but bounded** Windows x86 compatibility work:

- **PE32 image loading:** checked x86 PE sections, HIGHLOW relocations, compiler-style import tables and diagnosed failure reasons; a 2 MiB ring-3 guest address space is still the hard limit.
- **MSVCRT subset:** eleven compiler-callable `msvcrt.dll` memory/string/heap imports, including `malloc/free/calloc/realloc`, tested by a separate C-compiled PE32 with two ordinary import descriptor tables. These are built-in trampolines, not an actual Microsoft CRT DLL.
- **Win32 guest virtual memory:** restricted `VirtualAlloc(NULL, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)` and `VirtualFree(base, 0, MEM_RELEASE)` in a single 128 KiB isolated, 4 KiB-aligned arena. The arena rejects executable memory, arbitrary addresses, separate commit/reserve, decommit, `VirtualProtect`, mapping files and NT virtual-memory semantics; released pages are cleared.
- **Built-in DLL discovery:** `LoadLibraryA`, `GetProcAddress`, and `FreeLibrary` for **virtual** `kernel32.dll` and `msvcrt.dll` exports. Module reference counts, names, guest pointers and unsupported exports are validated. No external DLL is loaded, no image is relocated or linked dynamically, and no `DllMain`/TLS is run. Arbitrary third-party DLLs are rejected.
- **Compiler-built guest tests:** `apps/win32_realc.c` runs a genuine MinGW-generated Windows PE32 process in QEMU, dynamically calls API symbols obtained through `GetProcAddress`, checks page allocation/release and invalid-handle behavior, prints through `WriteFile`, and exits zero. Separate host tests cover the memory arena and virtual DLL export handling.
- **Storage regression:** the real downloaded Chrome installer and independent 7-Zip utility can be stored in a FAT16 guest disk; two QEMU boots of the **same** Windows-only disk independently validate persistent `WIN32.TXT` and the compiler-built Windows samples. Mixed Windows+macOS download tests remain experimental; they exercise the ATA retry path.

**Verified commit:** `02f28d8eb4437491fe7256f94f77124f58b58468`; Windows compatibility QEMU and two reboot checks: https://github.com/darkness22s/Bordify-OS/actions/runs/42566192421 ; normal Bordify image build: https://github.com/darkness22s/Bordify-OS/actions/runs/42566192438 ; combined package diagnostic: https://github.com/darkness22s/Bordify-OS/actions/runs/42566192382 .

**Still absent:** *general dynamic DLL loading and its dependency graph*, production C runtime initialization, `USER32`/`GDI32` Windows and GUI event loop, real multi-thread scheduling, TLS, Windows structured exceptions and unwind, general address-space management, and **native x64 Windows execution** (the existing x64 interpreter only supports restricted fixture instructions). Chrome and 7-Zip still cannot run. Green CI here verifies limited console workload and safe rejection, **not Windows compatibility for most applications**.

Bordify OS currently runs a 32-bit protected-mode x86 kernel, with built-in applications linked directly into the kernel image. There is now a single-process experimental ring-3 model, a small user-memory mapping and syscall ABI, but **not** a production process scheduler, dynamic linker, 64-bit CPU mode, arbitrary-app sandbox or general-purpose Windows/macOS loader. It uses FAT16 8.3 filenames and has a fixed kernel heap. These are hard architectural constraints, not missing file associations.


## Requested five-feature acceptance status (do not relabel without real app tests)

| Requested feature | Verified now | Not yet working |
| --- | --- | --- |
| Windows x64 EXE installers | 🟢 **Audited built-in installer fixture only:** real PE32+ AMD64 setup invokes `CreateFileA/WriteFile/CloseHandle`, writes a second PE32+ app onto FAT16, which Bordify reads and runs; GitHub Actions verifies persistence byte-for-byte | ⚠️ **General installer feature is still incomplete:** downloaded third-party x64 EXEs, NSIS/Inno/MSI setup, GUI, DLLs, broad Win32 APIs, arbitrary filesystem paths, production isolation, native 64-bit execution |
| macOS DMG mounting and installation | 🟢 **Audited fixture only:** zlib UDIF read-only HFS+ mount, root catalog lookup, data-fork extraction, persistent FAT16 installation and ARM64 Mach-O execution pass QEMU | ⚠️ **General DMG installation remains incomplete:** unsupported codecs, APFS, general HFS+ B-trees/extents and partitions, `.app` bundles, PKG scripts, arbitrary third-party installers |
| Apple Silicon ARM64 execution | 🟢 **Compiled workload subset:** independent Clang-generated Apple ARM64 Mach-O C loop (70 guest instructions) and assembled arithmetic/stack/function-call program (55 instructions) run through x86 QEMU and exit successfully; invalid opcodes/branches/addresses rejected | ⚠️ **General ARM64/macOS applications not supported:** full A64/AArch64/NEON/SVE instruction coverage, multiple Mach-O segments, dyld, Objective-C/Swift dependencies, real Darwin application APIs, device/OS services |
| Windows compatibility runtime | 🟢 **Audited file-I/O workload only:** genuine native x86 PE32 running in ring 3 imports seven `kernel32` calls; `CreateFileA` → `WriteFile` → `CloseHandle` → `CreateFileA` → `ReadFile` → byte-compare → `ExitProcess` persists `APPS/WIN32.TXT`; GitHub Actions independently extracts and checks contents | ⚠️ **General Win32 runtime incomplete:** dynamically linked DLLs, USER32/GDI32 GUI APIs, multithreading, exceptions, registry, NT file semantics, general filesystem permissions/path support, arbitrary downloaded EXEs and production isolation |
| macOS Darwin/Cocoa runtime | 🟢 **Audited linked Objective-C ABI test only:** real Clang-assembled Apple ARM64 Mach-O has 25 resolved `objc_getClass`/`sel_registerName`/`objc_msgSend` calls; modeled `NSString` string/length, `NSWindow` title/visibility, and `NSApplication` run/terminate lifecycle pass in x86 QEMU | ⚠️ **General macOS runtime incomplete:** true dyld/libobjc and framework binaries, Cocoa rendering/event loop, UI input, Swift, dispatch, broader Darwin calls, multiple objects/windows, normal app bundles, macOS ABI/loader completeness and independent third-party applications |

**Acceptance policy:** Green for a specific audited or compiler-generated workload means **only that tested workload** is verified in CI, not general application compatibility. The x64 installer, HFS+ DMG, Apple ARM64, Win32 persistent file API, and bounded Darwin/Objective-C lifecycle *specific test gates* now pass. General Windows/macOS application support is not production-ready until multiple independently produced real-world applications, dependencies and installers pass secure end-to-end tests.

## End-state goals

- Launch a useful, clearly defined subset of **Windows x86 applications** using a PE32 loader and a clean-room Win32-compatible runtime (or a Wine-derived implementation if the required POSIX and other dependencies can be ported).
- Launch a useful subset of **macOS applications** after implementing Mach-O loading and sufficient Darwin, Objective-C, Foundation, and Cocoa/AppKit compatibility. macOS software is not equivalent to Mach-O format support; compatibility will be app-specific and Apple software licensing/redistribution must be respected.
- Support **64-bit Intel and ARM** applications after a deliberate 64-bit migration, or through CPU emulation / binary translation where practical. Apple's modern Mac applications generally target x86-64 or ARM64 and often depend on extensive frameworks.
- Revisit **Android** via a substantial hosted Android runtime (Linux-kernel compatibility, drivers, or a guest VM/emulator). A full Android emulator is not feasible in the present 32-bit 128 MiB QEMU configuration.
- Treat **iPhone simulators** as a separate, much harder effort requiring a compatible mobile OS/framework environment; Apple's simulator components have platform and licensing restrictions. Do not imply that a Mach-O loader can run iOS apps.

## HFS+ allocation bitmap verification (security hardening)

The restricted read-only HFS+ catalog/file extractor now checks the volume's
allocation-file fork metadata and requires its first extent to contain the
complete bitmap. Before using the catalog extent or extracting file bytes,
the reader checks each allocation block's MSB-first allocation bit through
the same bounded, read-only UDIF sector callback. An extent that points at
a free block is rejected even if the physical disk-image sector exists.
Malformed, fragmented, or unavailable bitmap metadata fails closed.
The synthetic HFS+ fixture has one allocation-bitmap extent; this change
does **not** implement extent-overflow trees, general HFS+ mounts, APFS,
or arbitrary macOS application installation.

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

- **DMG:** The experimental UDIF reader now decodes the first XML `blkx`/base64 `mish` block map, validates raw, zero-fill and zlib chunk bounds, reads actual 512-byte sectors from a FAT16-hosted DMG **without extracting the whole image**, including bounded zlib-compressed chunks with Adler-32 validation; other compression algorithms are explicitly unsupported. QEMU verifies `APPS/DMGDEMO.DMG` end-to-end. **Still required:** multiple partitions/maps, LZFSE/bzip2/other codecs, UDIF checksum validation, a reusable block-device abstraction, and HFS+/APFS filesystem mounting, plus installer workflows. A DMG is a virtual disk image, not necessarily an installer.
- **macOS PKG:** Implement a sandboxed XAR reader, compression and package manifest parsing. Installation scripts and dependency management must be handled explicitly, with clear trust/permission policy.
- **macOS APP:** Support long filenames, symlinks, bundle `Info.plist`, architecture selection and code-signature validation policy before passing executable to an isolated Mach-O process runtime.
- **Windows x64 EXE installer:** Beyond PE32+ recognition, needs long-mode/64-bit processes, 64-bit Windows APIs, dynamic DLL loading, registry semantics, installer file/shortcut operations and a target app runtime. Self-extractors such as NSIS/Inno/InstallShield are different formats layered atop PE; compatibility must be tested individually.
- **MSI:** OLE/CFB signature alone is not proof of a Windows Installer database; use the MSI directory/stream structures to validate and install under a Windows Installer-compatible runtime.
- **Hardware:** Plan a x86-64 kernel and 64-bit boot path while keeping the current 32-bit boot image working until a tested replacement exists.

### Stage 0.9 — Bounded HFS+ root-file extent and allocation validation (verified)

A narrow read-only `hfsplus_read` path can locate a root-directory ASCII-named file in a **single 512-byte catalog leaf** and read its first contiguous data-fork extent through UDIF. The generated `MOUNT.DMG` fixture contains an actual catalog leaf pointing to a tiny generated `DARWIN.MCH` Mach-O payload, rather than only a catalog header. This is a synthetic, constrained HFS+ sample, **not** a general HFS+ mount, APFS parser, or macOS installer.

The latest hardening stores the validated HFS+ volume allocation-block count in the mount and rejects file extents that exceed it, including caller-forged first blocks or extent lengths even when raw backing sectors are otherwise accessible. Unit tests cover forged file extents, malformed catalog offsets, invalid leaf records, invalid root parent IDs, corrupted filesystem signatures and read errors. Catalog overflow extents, multi-node B-trees, non-ASCII names, symlinks, compression and larger files remain unsupported. Do not execute any extracted payload with kernel privilege.

### Stage 0.7b — Earlier synthetic HFS+ catalog-header fixture (verified)

The read-only HFS+ probe now validates the catalog fork's logical size and first extent before calculating the first catalog sector. It reads that sector through the existing UDIF reader and checks a 512-byte B-tree header node, its descriptor, tree depth, root/leaf node indexes, node counts, and record-offset table. Unsupported larger node sizes are rejected rather than partially interpreted. Both the Files inspector and QEMU integration fixture exercise this path. The **HFSPLUS.DMG header-only fixture** has no usable leaf records. A **different fixture**, MOUNT.DMG, supplies a valid root catalog leaf and supports bounded read-only extraction as described in Stage 0.8 below; neither is a general HFS+ mount.

### Stage 0.8 — Read-only HFS+ mount + contained app extraction and execution (QEMU verified)

`kernel/hfsplus_read.c` builds a bounded, read-only HFS+ mount on the existing UDIF `blkx` sector reader. It validates HFS+ volume and catalog B-tree headers, supports a **single 512-byte root leaf** in the first catalog extent, parses UTF-16BE ASCII catalog keys, recognizes a root file data-fork record, validates its first allocation extent, and extracts up to 16 KiB from a zlib-compressed DMG. Larger B-trees, extents and arbitrary filesystem writes are unsupported.

`tools/genhfsmount.py` generates a small **HFS+ test volume** with allocation bitmap, catalog header/leaf and data fork containing the real ARM64 Mach-O fixture `DARWIN.MCH`, wrapped as `APPS/MOUNT.DMG`. It is neither a general HFS+ formatter nor a genuine Apple `.app`/PKG installer. Opening that audited DMG in Files, or the boot test, extracts its `DARWIN.MCH` to FAT16 `APPS/DMGAPP.MCH`, verifies the file and runs it in the limited ARM64 Darwin interpreter. QEMU's GitHub Actions job independently extracts the **persisted file** using `mcopy`, compares it with the original Mach-O using `cmp`, and requires successful boot and app execution: https://github.com/darkness22s/Bordify-OS/actions/runs/42507837922.

Host tests cover compressed UDIF → read-only mount → catalog record lookup → data-fork extraction and malformed record offsets, root IDs, and extents. This makes **the audited sample DMG extraction/install flow** a green *test gate only*. **General macOS DMG mounting and installation must not be labeled green.** Missing capabilities include APFS, full HFS+ folder hierarchies and B-tree traversals, multi-extent files, all codecs, macOS `.app` bundles, PKG scripts, system frameworks, security policies and real third-party installers.

### Stage 0.7 — HFS+ and HFSX volume-header probing (verified)

Apple's HFS Plus Volume Format (TN1150) defines a 512-byte big-endian volume header beginning at byte offset 1,024 of a filesystem volume. Bordify now checks signature/version pairing (`H+` version 4 and `HX` version 5), validates allocation-block power-of-two and logical sector limits, free-block counts and the catalog fork's first extent. It exposes read-only HFS+ metadata (block size, volume size, free blocks, file/folder counts, journal flag, and first catalog extent) in the DMG inspector. Host unit tests cover malformed and valid headers, while an **artificial compressed DMG fixture** `APPS/HFSPLUS.DMG` verifies zlib decompression and sector-two HFS+ header parsing during QEMU startup.

This fixture does **not** have a valid catalog B-tree. Bordify **cannot** browse files inside HFS+ volumes or install macOS applications yet. Next steps are a trusted read-only HFS+ B-tree/catalog implementation and access to complete disk-image partitions, then APFS and installer workflows.

### Stage 0.6 — Bounded zlib-compressed DMG reads (verified)

A freestanding, checked RFC 1950/1951 zlib/DEFLATE decoder supports stored, fixed-Huffman and dynamic-Huffman streams with Adler-32 validation and a strict output bound. The UDIF sector reader now recognizes blkx compression type `0x80000005` (zlib), stages at most 1 MiB per chunk, validates exact decompressed sector size and allows read-only sector access. Host-side tests compare against the system zlib encoder and exercise corruption, truncated streams and output overruns; a generated `APPS/ZDMG.DMG` contains a real compressed data fork and its sector contents are verified during a QEMU boot. This does **not** yet mount a filesystem or install an application, and only supports zlib chunks within the size limit. A DMG may contain non-zlib chunks, APFS/HFS+ filesystem data, or no installer at all.

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

### Stage 1.4b — Clang-compiled Apple ARM64 user-code execution (QEMU verified)

The original 7-instruction custom-ABI demonstration has now been supplemented with **two independently assembled/compiled ARM64 Mach-O workloads**, using the Apple ARM64 target `clang -target arm64-apple-macos11` and a checked no-relocations Mach-O object wrapper, rather than hand-encoding test instructions:

- `APPS/ARMWORK.MCH`: a real Apple ARM64 **assembly source** with conditional loops, add/sub with N/Z/C/V flags, ARM64 B.cond, BL/RET, stack-frame setup, pair stores, and bounded 64-bit LDR/STR. Under x86 QEMU its guest executes **55 instructions** and emits a serial message through the opt-in Darwin BSD `write` syscall.
- `APPS/ARMCALC.MCH`: **real C source** compiled with Clang `-O1 -ffreestanding` for Apple ARM64. It computes a volatile checksum on the emulated stack. Real compiler-generated MOV W/X, ADD/SUB, CMP, B.cond and CSEL instructions execute. Under x86 QEMU it executes **70 guest instructions** and exits 0 through the test Darwin BSD exit API; a host test changes the comparison and verifies exit 99 instead.

The interpreter now supports ARMv8-A register arithmetic (immediate and LSL register forms), MOVN/Z/K, conditional select, B/BL, B.cond, CBZ/CBNZ, RET, unsigned-offset 32/64-bit LDR/STR and limited STP/LDP stack operations. It tracks 64-bit registers, a separate guest stack pointer and NZCV condition flags; memory operations remain within a bounded 64 KiB guest, while guest writes into the loaded image are rejected. All execution is **instruction-by-instruction interpretation** on the existing 32-bit x86 kernel: it is NOT an ARM64 native kernel or Rosetta/Apple Silicon virtualization.

Host tests cover successful compiler-generated behavior, deliberate incorrect comparisons, infinite instruction loops, unsupported opcodes, out-of-range guest addresses and unsupported CPU formats. The successful QEMU run verifies both independent workloads and desktop startup: https://github.com/darkness22s/Bordify-OS/actions/runs/42509399305.

**Scope of the green test:** *Apple Silicon ARM64 Mach-O execution for these small, self-contained programs only.* Unknown/third-party apps are NOT generally runnable: full ARM64 ISA incl. SIMD/NEON and atomics, larger/multi-segment Mach-O loading, dynamic linking, framework dependencies, multiprocessing, complete Darwin syscall and Cocoa environments, hardened process sandboxing and a broad compatibility suite are not implemented. Do not advertise unrestricted Apple Silicon macOS application compatibility.

### Stage 1.5b — Native ring-3 Win32 kernel32 persistent-file API (QEMU verified)

The x86 PE32 loader now imports **seven** recognized `kernel32.dll` functions: `ExitProcess`, `GetStdHandle`, `WriteFile`, `GetLastError`, `CreateFileA`, `ReadFile`, and `CloseHandle`. The bridge stubs are real x86 stdcall instructions staged inside the isolated CPL3 process memory. They marshal parameters into Bordify's `int 0x80` syscall interface; host kernel pointers are not exposed to the guest.

`kernel/win32_files.c` implements a single bounded file handle (`0x200`) and 1 KiB file staging buffer. For this audited proof, **only `C:\\BORDIFY\\WIN32.TXT`** is accessible, mapping to the FAT16 file `APPS/WIN32.TXT`. Opening with `GENERIC_WRITE` + `CREATE_ALWAYS` stages new data; `WriteFile` validates user addresses and lengths; `CloseHandle` commits to FAT16. Opening with `GENERIC_READ` + `OPEN_EXISTING` reloads the saved data and `ReadFile` copies it back into ring-3 memory. Invalid handles, disallowed paths, oversized writes, bad pointers, and failed commits return appropriate bounded Win32 errors. This is intentionally a **restricted, explicit storage capability**, not arbitrary filesystem access.

`tools/genwin32io.py` generates a real x86 PE32 console executable `APPS/WINIO.EXE` with seven IAT imports and Win32 stdcall calls. The application creates the fixed file, writes a test message, closes it, opens it again, reads it into a second buffer, byte-compares contents **inside the guest**, prints a success message using `GetStdHandle/WriteFile`, and exits zero through `ExitProcess`. The old `WINHELLO.EXE` stdout test still runs unchanged. In QEMU GitHub Actions, `WIN32.TXT` is extracted *independently from the written guest disk* via `mcopy` and compared byte-for-byte with the expected payload using `cmp`. Host unit tests also cover unauthorized paths, invalid modes, pointer errors, invalid handles, disk-full limits, and rejected commits. Verified run: https://github.com/darkness22s/Bordify-OS/actions/runs/42512100464.

**Green means the bounded Win32 file-I/O workflow passes end-to-end.** It does **not** mean arbitrary third-party Windows desktop programs can execute. The runtime still lacks general NT handles, arbitrary paths, directory operations, GUI/USER32/GDI32, DLL loading, exceptions, threading, actual CRT compatibility, installer APIs, and production-grade guest isolation.

### Stage 1.5 — Restricted real Win32 PE32 console execution (in this PR)

Implemented a bounded PE32 loader for a single x86 executable at the fixed user base `0x40000000`, with checked sections, import names and a four-function `kernel32.dll` bridge implemented as ring-3 stdcall trampolines: `ExitProcess`, `GetStdHandle`, `WriteFile`, and `GetLastError`. The independently generated `APPS/WINHELLO.EXE` is a real PE32 executable with a proper import table; it calls `GetStdHandle`, `WriteFile`, and `ExitProcess`. The VM boot test requires its console output and exit status, and it can be launched from the Files Apps sidebar. Unsupported machine types, invalid headers, unknown imports, relocation-dependent images and non-console executables are deliberately refused. This is **not** general Windows compatibility; it supports an intentionally tiny console-only API subset and must not be used to run untrusted installers. Full x64 installer execution is not implemented; a **separate, interpreted PE32+ proof** is described below.

### Stage 1.6 — PE32+ AMD64 console interpreter (QEMU verified)

`APPS/WIN64.EXE` is a **real PE32+ AMD64** sample with a 64-bit PE optional header, a mapped x64 section, import directory, 64-bit IAT thunks, and Microsoft x64 calling-convention arguments. The freestanding `kernel/pe64_vm.c` interpreter checks headers, sections, import strings, bounds and instruction count before stepping a very small x64 instruction subset. Its guest-only call dispatcher models `GetStdHandle`, `WriteFile`, `ExitProcess` and `GetLastError` from `kernel32.dll`. The guest program prints through `WriteFile` and exits successfully; tests reject malformed PE32+, imports, unsupported instructions and invalid guest memory. Boot and Files can launch this audited sample.

**Important:** x64 code is **interpreted**, not natively run in CPU long mode. No general x64 executables, Win64 DLLs, Windows GUI, installer engines, unrestricted installation operations, permissions or registry are implemented; an audited installer write subset is described below. This is a *yellow foundation for the Windows x64 installer roadmap*, not a working x64 installer feature.

### Stage 1.8 — Restricted Windows x64 installer execution, persistent FAT16 integration (QEMU verified)

`tools/genwin64setup.py` produces `APPS/SETUP64.EXE`, a genuine self-contained **PE32+ x86-64** executable which embeds a second valid PE32+ application. It runs via the existing bounded AMD64 instruction interpreter using real Win64 API imports and calling conventions. Added `CreateFileA`, `WriteFile` and `CloseHandle` semantics stage up to 8 KiB of installer output and commit it through an explicit host callback **only** to the audited `C:\\APP64.EXE` path, mapped to `/APPS/APP64.EXE` on FAT16. Guest-controlled arbitrary paths and API calls are rejected.

During boot, the setup is loaded from FAT16, executes in the interpreter, writes the payload, closes the handle, and verifies disk content. Bordify then loads and **executes the installed PE32+ x64 app**. The Files application can also launch `SETUP64.EXE` and the resulting `APP64.EXE`. The host tests check creation, installed program execution, disallowed filenames, malformed imports, and callback failure. The QEMU CI test uses an independent **writable copy** of the boot image; after guest execution it extracts `APPS/APP64.EXE` via `mcopy` and `cmp` checks byte-for-byte against the known payload. GitHub Actions run: https://github.com/darkness22s/Bordify-OS/actions/runs/42506291866.

**Security and compatibility limits:** This is an audited *synthetic sample installer*, not NSIS, Inno Setup, MSI, or a genuine third-party installer. The interpreter supports a tiny x86-64 instruction subset; it cannot run most independent x64 binaries or dependencies. It stages its writes in kernel memory, has no per-process sandbox or transaction rollback for unexpected filesystem I/O failures, and remains unsafe for untrusted code. Green applies **only to this one narrow installation test**. Full Windows x64 installers are still incomplete.

### Stage 1.7b — Clang-assembled Objective-C ABI linkage and modeled AppKit lifecycle (QEMU verified)

The previous `SVC #0x81` call-number bridge has been extended with **actual Apple ARM64 Objective-C calling-convention entry points**. `apps/cocoa_lifecycle.S` is assembled with `clang -target arm64-apple-macos11` as a real relocatable Mach-O `MH_OBJECT`. It imports unresolved external symbols `_objc_getClass`, `_sel_registerName` and `_objc_msgSend`. The new bounded static linker `tools/linkcocoaobj.py` parses its `LC_SYMTAB` and `ARM64_RELOC_BRANCH26` records; only those three explicitly allowlisted symbols can be bound. It produces a genuine `MH_EXECUTE` image with `LC_SEGMENT_64`, `LC_MAIN` and an explicit **Bordify-specific `LC_NOTE` import manifest** containing exactly three validated trampoline offsets. The ARM64 interpreter follows normal `BL` instructions into the stubs, dispatches the symbols using the ARM64 `x0/x1/x2` Objective-C ABI, and returns through `x30`. This is a **restricted build-time binding mechanism, not Apple's dyld or dynamic linker**.

The runtime now supports a single in-memory `NSString` object (`stringWithUTF8String:`, `UTF8String`, `length`), a modeled `NSWindow` (`new`, `setTitle:`, `makeKeyAndOrderFront:`, `isVisible`, `orderOut:`), and the `NSApplication` lifecycle (`sharedApplication`, `run`, `isRunning`, `terminate:`). Unsupported classes, selectors, invalid receivers or guest pointers fail closed. **The NSWindow is a state model, NOT an on-screen Cocoa/AppKit window; `run` advances the modeled lifecycle but does not create an AppKit event loop.**

The included `APPS/COCOA.MCH` application makes **25 statically linked Objective-C calls**, checks the `NSString` length and application state transitions, sets an `NSWindow` title and visibility, prints its stored NSString through the Darwin-style BSD `write` bridge and exits zero. Host tests validate positive lifecycle state and malformed `LC_NOTE`, corrupted import stubs, unrecognized Objective-C classes and bounded instruction loops. QEMU boots Bordify, launches the same guest from FAT16, checks its serial output and linked-import count, and continues to the desktop without panic: https://github.com/darkness22s/Bordify-OS/actions/runs/42513324153.

**Green only for the audited, compiler-assembled Objective-C lifecycle test.** This is not a real Apple `libobjc`, full `Foundation`, `AppKit`, `Cocoa`, dyld, Swift, Quartz graphics, window server, input event loop or general macOS application runtime. Future work must implement genuine framework contracts and test independent macOS programs before making any general compatibility claim.

### Stage 1.7 — Experimental ARM64 Darwin BSD/Objective-C bridge (QEMU verified)

The ARM64 interpreter has an opt-in Darwin mode in `a64_macho_run_darwin` while preserving its original strict Bordify demo mode. Darwin mode handles a bounded subset of ARM64 `SVC #0x80` BSD calls (`write`, `exit`, deterministic `getpid`) and a **test-only** `SVC #0x81` bridge implementing limited `objc_getClass`, `sel_registerName`, and `objc_msgSend` semantics. The bridge supports `NSString stringWithUTF8String:`, `UTF8String`, and `NSApplication sharedApplication`/`isRunning`, with validated in-guest strings, memory bounds and unsupported-selector rejection.

A generated **real Mach-O ARM64** test binary `APPS/DARWIN.MCH` calls the bridge and Darwin-style BSD write, producing expected serial output and exiting under x86 QEMU; host negative tests also verify fail-closed behavior. Both demos can be launched from Files. However, `SVC #0x81` is a Bordify-specific bridging convention—not actual `libobjc` dynamic-symbol linkage. There is **no dyld, Cocoa GUI/event loop, real macOS framework implementation, or arbitrary Apple application execution**. Do not confuse this research shim with a supported Darwin/Cocoa runtime.

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

### HFS+ fail-closed catalog and extraction hardening (2026-11-08)

The restricted root-directory HFS+ reader rejects depth-one leaf nodes with sibling links, since following sibling chains is not implemented. It also rejects file forks whose total allocation-block count differs from the first extent, rather than silently treating a fragmented file as fully extracted. If a later sector read fails during extraction, previously copied output bytes are cleared before returning failure; callers must still honor the boolean result and must not publish data on failure. Host regression tests exercise both sibling-link directions and the fragmented-fork rejection. This does **not** implement general HFS+ B-tree traversal, overflow extents, APFS or third-party DMG installation.
