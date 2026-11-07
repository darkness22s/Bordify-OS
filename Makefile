CC ?= gcc
PYTHON ?= python3

CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
          -fno-common -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -fno-asynchronous-unwind-tables \
          -fno-unwind-tables -Iinclude -Ikernel
ASFLAGS := -m32
LDFLAGS := -m32 -no-pie -nostdlib -Wl,--build-id=none -T boot/linker.ld

BUILD := build
KERNEL := $(BUILD)/bordify.elf
CFILES := $(wildcard kernel/*.c)
AFILES := boot/boot.S kernel/isr.S
OFILES := $(patsubst kernel/%.c,$(BUILD)/kernel/%.o,$(CFILES)) \
          $(patsubst %.S,$(BUILD)/%.o,$(AFILES)) \
          $(BUILD)/font.o

all: $(KERNEL)

$(BUILD)/font.c: tools/genfont.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/genfont.py $@

$(BUILD)/font.o: $(BUILD)/font.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot/%.o: boot/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OFILES) boot/linker.ld
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(OFILES) -lgcc

clean:
	rm -rf $(BUILD) dist

.PHONY: all clean
