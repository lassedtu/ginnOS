CROSS   ?= i686-elf-
AS      := nasm
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
QEMU    := qemu-system-i386

BUILD_DIR := build

STAGE1_SRC := src/bootloader/stage1/boot.asm
STAGE2_SRC := src/bootloader/stage2/main.asm
STAGE2_C_SRC := src/bootloader/stage2/main.c
STAGE2_STDIO_SRC := src/bootloader/stage2/stdio.c
STAGE2_X86_ASM_SRC := src/bootloader/stage2/x86.asm

STAGE1_BIN := $(BUILD_DIR)/boot/stage1.bin
STAGE2_OBJ := $(BUILD_DIR)/boot/stage2.o
STAGE2_C_OBJ := $(BUILD_DIR)/boot/stage2_main.o
STAGE2_STDIO_OBJ := $(BUILD_DIR)/boot/stage2_stdio.o
STAGE2_X86_ASM_OBJ := $(BUILD_DIR)/boot/stage2_x86_asm.o
STAGE2_ELF := $(BUILD_DIR)/boot/stage2.elf
STAGE2_BIN := $(BUILD_DIR)/boot/stage2.bin
STAGE2_PAD := $(BUILD_DIR)/boot/stage2.padded.bin

KERNEL_MAIN_SRC  := src/kernel/main.asm
KERNEL_C_SRC     := src/kernel/kernel.c
KERNEL_MAIN_OBJ  := $(BUILD_DIR)/kernel/main.o
KERNEL_C_OBJ     := $(BUILD_DIR)/kernel/kernel.o
KERNEL_ELF       := $(BUILD_DIR)/kernel.elf
KERNEL_BIN       := $(BUILD_DIR)/kernel.bin
KERNEL_PAD       := $(BUILD_DIR)/kernel.padded.bin

DISK_IMAGE := $(BUILD_DIR)/ginnos.img

STAGE2_SECTORS := 4
KERNEL_SECTORS := 12
SECTOR_SIZE    := 512
STAGE2_MAX_BYTES := $(shell echo $$(( $(STAGE2_SECTORS) * $(SECTOR_SIZE) )))
KERNEL_MAX_BYTES := $(shell echo $$(( $(KERNEL_SECTORS) * $(SECTOR_SIZE) )))

CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32
STAGE2_CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m16 -fno-pic -fno-stack-protector
LDFLAGS := -T linker/kernel.ld -nostdlib
STAGE2_LDFLAGS := -T linker/stage2.ld -nostdlib

.PHONY: all run clean check-tools

all: check-tools $(DISK_IMAGE)

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "Missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "Missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "Missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "Missing tool: $(QEMU)"; exit 1; }

$(STAGE1_BIN): $(STAGE1_SRC)
	mkdir -p $(dir $@)
	$(AS) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512 || { echo "stage1 must be exactly 512 bytes"; exit 1; }

$(STAGE2_BIN): $(STAGE2_SRC) $(STAGE2_C_SRC) $(STAGE2_STDIO_SRC) $(STAGE2_X86_ASM_SRC) linker/stage2.ld
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(STAGE2_SRC) -o $(STAGE2_OBJ)
	$(CC) $(STAGE2_CFLAGS) -c $(STAGE2_C_SRC) -o $(STAGE2_C_OBJ)
	$(CC) $(STAGE2_CFLAGS) -c $(STAGE2_STDIO_SRC) -o $(STAGE2_STDIO_OBJ)
	$(AS) -f elf32 $(STAGE2_X86_ASM_SRC) -o $(STAGE2_X86_ASM_OBJ)
	$(LD) $(STAGE2_LDFLAGS) -o $(STAGE2_ELF) $(STAGE2_OBJ) $(STAGE2_C_OBJ) $(STAGE2_STDIO_OBJ) $(STAGE2_X86_ASM_OBJ)
	$(OBJCOPY) -O binary $(STAGE2_ELF) $(STAGE2_BIN)
	@test $$(wc -c < $(STAGE2_BIN)) -le $(STAGE2_MAX_BYTES) || { echo "stage2 exceeds $(STAGE2_MAX_BYTES) bytes"; exit 1; }

$(STAGE2_PAD): $(STAGE2_BIN)
	cp $(STAGE2_BIN) $@
	truncate -s $(STAGE2_MAX_BYTES) $@

$(KERNEL_MAIN_OBJ): $(KERNEL_MAIN_SRC)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(KERNEL_C_OBJ): $(KERNEL_C_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_MAIN_OBJ) $(KERNEL_C_OBJ) linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_MAIN_OBJ) $(KERNEL_C_OBJ)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@test $$(wc -c < $@) -le $(KERNEL_MAX_BYTES) || { echo "kernel exceeds $(KERNEL_MAX_BYTES) bytes"; exit 1; }

$(KERNEL_PAD): $(KERNEL_BIN)
	cp $(KERNEL_BIN) $@
	truncate -s $(KERNEL_MAX_BYTES) $@

$(DISK_IMAGE): $(STAGE1_BIN) $(STAGE2_PAD) $(KERNEL_PAD)
	truncate -s 1474560 $@
	dd if=$(STAGE1_BIN) of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=$(STAGE2_PAD) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_PAD) of=$@ bs=512 seek=5 conv=notrunc status=none

run: check-tools $(DISK_IMAGE)
	$(QEMU) -drive if=floppy,format=raw,file=$(DISK_IMAGE)

clean:
	rm -rf $(BUILD_DIR)
