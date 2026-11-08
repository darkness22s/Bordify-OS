CC ?= gcc
PYTHON ?= python3

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
          -fno-common -fno-strict-aliasing -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -Iinclude \
          -Ithird_party/bearssl/inc -Ithird_party/bearssl/src
KERNEL_CFLAGS := $(CFLAGS) -Ikernel
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

all: $(KERNEL) $(BUILD)/apps/hello.elf $(BUILD)/apps/fault.elf

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

test: $(BUILD)/tests/exec_inspect_test $(BUILD)/tests/package_inspect_test
	./$(BUILD)/tests/exec_inspect_test
	./$(BUILD)/tests/package_inspect_test

clean:
	rm -rf $(BUILD) dist

.PHONY: all clean test
