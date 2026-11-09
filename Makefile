CC ?= gcc
PYTHON ?= python3
CLANG ?= clang
MINGW_CC ?= i686-w64-mingw32-gcc

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
          -fno-common -fno-strict-aliasing -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -Iinclude \
          -Ithird_party/bearssl/inc -Ithird_party/bearssl/src
KERNEL_CFLAGS := $(CFLAGS) -Ikernel
ifeq ($(CHROME_PROBE),1)
KERNEL_CFLAGS += -DBORDIFY_CHROME_PROBE=1
endif
BEARSSL_CFLAGS := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 \
                  -DBR_USE_WIN32_RAND=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
                  -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0 \
                  -DBR_64=0 -DBR_INT128=0 -DBR_UMUL128=0
ASFLAGS := -m32
LDFLAGS := -m32 -no-pie -nostdlib -Wl,--build-id=none -T boot/linker.ld

BUILD := build
KERNEL := $(BUILD)/bordify.elf
CFILES := $(wildcard kernel/*.c)
AFILES := boot/boot.S kernel/isr.S kernel/user_entry.S
BEARSSL_SRCS := $(wildcard third_party/bearssl/src/*.c third_party/bearssl/src/*/*.c)
BEARSSL_OFILES := $(patsubst third_party/bearssl/src/%.c,$(BUILD)/bearssl/%.o,$(BEARSSL_SRCS))
OFILES := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(CFILES)) \
          $(patsubst %.S,$(BUILD)/%.o,$(AFILES)) \
          $(BUILD)/font.o $(BEARSSL_OFILES)

all: $(KERNEL) $(BUILD)/apps/hello.elf $(BUILD)/apps/fault.elf $(BUILD)/apps/winhello.exe $(BUILD)/apps/winio.exe $(BUILD)/apps/dmgdemo.dmg $(BUILD)/apps/zdmg.dmg $(BUILD)/apps/hfsplus.dmg $(BUILD)/apps/armhello.mch $(BUILD)/apps/win64demo.exe $(BUILD)/apps/win64setup.exe $(BUILD)/apps/darwin.mch $(BUILD)/apps/mount.dmg $(BUILD)/apps/armwork.mch $(BUILD)/apps/armcalc.mch $(BUILD)/apps/cocoa.mch

# A genuinely C-compiled Windows PE32 program with normal kernel32 DLL
# imports and a preferred image base that requires PE32 HIGHLOW rebasing.
$(BUILD)/apps/winreal.exe: apps/win32_realc.c
	@mkdir -p $(BUILD)/apps
	$(MINGW_CC) -Os -std=gnu11 -fno-stack-protector -nostdlib \
	  -Wl,--entry,_mainCRTStartup -Wl,--subsystem,console \
	  -Wl,--image-base,0x00400000 -Wl,--enable-reloc-section \
	  -o $@ $< -lkernel32

$(BUILD)/apps/win64setup.exe: tools/genwin64setup.py $(BUILD)/apps/win64demo.exe
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genwin64setup.py $@ $(BUILD)/apps/win64demo.exe

$(BUILD)/apps/win64demo.exe: tools/genwin64demo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genwin64demo.py $@

$(BUILD)/apps/mount.dmg: tools/genhfsmount.py $(BUILD)/apps/darwin.mch
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genhfsmount.py $@ $(BUILD)/apps/darwin.mch

$(BUILD)/apps/darwin.mch: tools/gendarwindemo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/gendarwindemo.py $@

$(BUILD)/apps/cocoa.o: apps/cocoa_lifecycle.S
	@mkdir -p $(BUILD)/apps
	$(CLANG) -target arm64-apple-macos11 -c $< -o $@

$(BUILD)/apps/cocoa.mch: $(BUILD)/apps/cocoa.o tools/linkcocoaobj.py
	$(PYTHON) tools/linkcocoaobj.py $(BUILD)/apps/cocoa.o $@

$(BUILD)/apps/armcalc.o: apps/armcalc.c
	@mkdir -p $(BUILD)/apps
	$(CLANG) -target arm64-apple-macos11 -O1 -ffreestanding -fno-stack-protector -c $< -o $@

$(BUILD)/apps/armcalc.mch: $(BUILD)/apps/armcalc.o tools/wraparm64obj.py
	$(PYTHON) tools/wraparm64obj.py $(BUILD)/apps/armcalc.o $@

$(BUILD)/apps/armwork.o: apps/armwork.S
	@mkdir -p $(BUILD)/apps
	$(CLANG) -target arm64-apple-macos11 -c $< -o $@

$(BUILD)/apps/armwork.mch: $(BUILD)/apps/armwork.o tools/wraparm64obj.py
	$(PYTHON) tools/wraparm64obj.py $(BUILD)/apps/armwork.o $@

$(BUILD)/apps/armhello.mch: tools/genarm64demo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genarm64demo.py $@

$(BUILD)/apps/hfsplus.dmg: tools/genudifdemo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genudifdemo.py $@ --hfsplus

$(BUILD)/apps/zdmg.dmg: tools/genudifdemo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genudifdemo.py $@ --zlib

$(BUILD)/apps/dmgdemo.dmg: tools/genudifdemo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genudifdemo.py $@

$(BUILD)/apps/winio.exe: tools/genwin32io.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genwin32io.py $@

$(BUILD)/apps/winhello.exe: tools/genwin32demo.py
	@mkdir -p $(BUILD)/apps
	$(PYTHON) tools/genwin32demo.py $@

$(BUILD)/apps/fault.o: apps/fault.S
	@mkdir -p $(BUILD)/apps
	$(CC) -m32 -c $< -o $@

$(BUILD)/apps/fault.elf: $(BUILD)/apps/fault.o
	ld -m elf_i386 -nostdlib --build-id=none -Ttext=0x40001000 -e _start $< -o $@

$(BUILD)/apps/hello.o: apps/hello.S
	@mkdir -p $(BUILD)/apps
	$(CC) -m32 -c $< -o $@

$(BUILD)/apps/hello.elf: $(BUILD)/apps/hello.o
	ld -m elf_i386 -nostdlib --build-id=none -Ttext=0x40001000 -e _start $< -o $@

$(BUILD)/font.c: tools/genfont.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/genfont.py $@

$(BUILD)/font.o: $(BUILD)/font.c
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/boot/%.o: boot/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/bearssl/%.o: third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BEARSSL_CFLAGS) -c $< -o $@

$(KERNEL): $(OFILES) boot/linker.ld
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(OFILES) -lgcc


# Build host-side parser tests without -m32 or the freestanding kernel flags.
HOST_CC ?= cc
$(BUILD)/tests/exec_inspect_test: tests/exec_inspect_test.c kernel/exec_inspect.c kernel/exec_inspect.h
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/exec_inspect_test.c kernel/exec_inspect.c -o $@

$(BUILD)/tests/package_inspect_test: tests/package_inspect_test.c kernel/package_inspect.c kernel/package_inspect.h
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/package_inspect_test.c kernel/package_inspect.c -o $@

$(BUILD)/tests/win32_pe_test: tests/win32_pe_test.c kernel/win32_pe.c kernel/win32_pe.h $(BUILD)/apps/winhello.exe
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/win32_pe_test.c kernel/win32_pe.c -o $@

$(BUILD)/tests/win32_files_test: tests/win32_files_test.c kernel/win32_pe.c kernel/win32_files.c kernel/win32_files.h $(BUILD)/apps/winio.exe
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/win32_files_test.c kernel/win32_pe.c kernel/win32_files.c -o $@

$(BUILD)/tests/udif_blkx_test: tests/udif_blkx_test.c kernel/udif_blkx.c kernel/udif_blkx.h kernel/zlib_inflate.c kernel/zlib_inflate.h
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/udif_blkx_test.c kernel/udif_blkx.c kernel/zlib_inflate.c -lz -o $@

$(BUILD)/tests/zlib_inflate_test: tests/zlib_inflate_test.c kernel/zlib_inflate.c kernel/zlib_inflate.h
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/zlib_inflate_test.c kernel/zlib_inflate.c -lz -o $@

$(BUILD)/tests/a64_macho_test: tests/a64_macho_test.c kernel/a64_macho.c kernel/a64_macho.h kernel/darwin_objc.c kernel/darwin_objc.h $(BUILD)/apps/armhello.mch
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/a64_macho_test.c kernel/a64_macho.c kernel/darwin_objc.c -o $@

$(BUILD)/tests/hfsplus_probe_test: tests/hfsplus_probe_test.c kernel/hfsplus_probe.c kernel/hfsplus_probe.h
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/hfsplus_probe_test.c kernel/hfsplus_probe.c -o $@

$(BUILD)/tests/pe64_vm_test: tests/pe64_vm_test.c kernel/pe64_vm.c kernel/pe64_vm.h $(BUILD)/apps/win64demo.exe
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/pe64_vm_test.c kernel/pe64_vm.c -o $@

$(BUILD)/tests/pe64_install_test: tests/pe64_install_test.c kernel/pe64_vm.c kernel/pe64_vm.h $(BUILD)/apps/win64setup.exe
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/pe64_install_test.c kernel/pe64_vm.c -o $@

$(BUILD)/tests/cocoa_lifecycle_test: tests/cocoa_lifecycle_test.c kernel/a64_macho.c kernel/darwin_objc.c kernel/darwin_objc.h $(BUILD)/apps/cocoa.mch
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/cocoa_lifecycle_test.c kernel/a64_macho.c kernel/darwin_objc.c -o $@

$(BUILD)/tests/a64_c_test: tests/a64_c_test.c kernel/a64_macho.c kernel/darwin_objc.c $(BUILD)/apps/armcalc.mch
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/a64_c_test.c kernel/a64_macho.c kernel/darwin_objc.c -o $@

$(BUILD)/tests/a64_workload_test: tests/a64_workload_test.c kernel/a64_macho.c kernel/a64_macho.h kernel/darwin_objc.c kernel/darwin_objc.h $(BUILD)/apps/armwork.mch
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/a64_workload_test.c kernel/a64_macho.c kernel/darwin_objc.c -o $@

$(BUILD)/tests/darwin_macho_test: tests/darwin_macho_test.c kernel/a64_macho.c kernel/darwin_objc.c kernel/darwin_objc.h $(BUILD)/apps/darwin.mch
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/darwin_macho_test.c kernel/a64_macho.c kernel/darwin_objc.c -o $@

$(BUILD)/tests/hfsplus_read_test: tests/hfsplus_read_test.c kernel/hfsplus_read.c kernel/hfsplus_probe.c kernel/udif_blkx.c kernel/zlib_inflate.c kernel/package_inspect.c $(BUILD)/apps/mount.dmg
	@mkdir -p $(BUILD)/tests
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel tests/hfsplus_read_test.c kernel/hfsplus_read.c kernel/hfsplus_probe.c kernel/udif_blkx.c kernel/zlib_inflate.c kernel/package_inspect.c -lz -o $@

test: $(BUILD)/tests/cocoa_lifecycle_test $(BUILD)/tests/win32_files_test $(BUILD)/tests/a64_c_test $(BUILD)/tests/a64_workload_test $(BUILD)/tests/hfsplus_read_test $(BUILD)/tests/pe64_vm_test $(BUILD)/tests/pe64_install_test $(BUILD)/tests/darwin_macho_test $(BUILD)/tests/exec_inspect_test $(BUILD)/tests/package_inspect_test $(BUILD)/tests/win32_pe_test $(BUILD)/tests/udif_blkx_test $(BUILD)/tests/a64_macho_test $(BUILD)/tests/zlib_inflate_test $(BUILD)/tests/hfsplus_probe_test
	./$(BUILD)/tests/cocoa_lifecycle_test
	$(PYTHON) tests/linkcocoaobj_test.py
	./$(BUILD)/tests/win32_files_test
	./$(BUILD)/tests/a64_c_test
	./$(BUILD)/tests/a64_workload_test
	./$(BUILD)/tests/hfsplus_read_test
	./$(BUILD)/tests/pe64_vm_test
	./$(BUILD)/tests/pe64_install_test
	./$(BUILD)/tests/darwin_macho_test
	./$(BUILD)/tests/exec_inspect_test
	./$(BUILD)/tests/package_inspect_test
	./$(BUILD)/tests/win32_pe_test
	./$(BUILD)/tests/udif_blkx_test
	./$(BUILD)/tests/a64_macho_test
	./$(BUILD)/tests/zlib_inflate_test
	./$(BUILD)/tests/hfsplus_probe_test

clean:
	rm -rf $(BUILD) dist

.PHONY: all clean test
