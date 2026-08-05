CROSS   ?= i686-elf-
AS      := nasm
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
QEMU    := qemu-system-i386
PYTHON  ?= python3

BUILD_DIR := build

STAGE1_SRC := src/bootloader/stage1/boot.asm
STAGE2_SRC := src/bootloader/stage2/main.asm
STAGE2_C_SRC := src/bootloader/stage2/main.c
STAGE2_OUTPUT_SRC := src/bootloader/stage2/output.c
COMMON_MEMORY_SRC := src/common/memory.c
COMMON_STRING_SRC := src/common/string.c
COMMON_STDIO_SRC := src/common/stdio.c
DRIVER_BLOCK_SRC := src/drivers/disk/block_device.c
DRIVER_PARTITION_SRC := src/drivers/disk/partition.c
DRIVER_ATA_SRC := src/drivers/disk/ata.c
FS_EXT2_SRC := src/fs/ext2/ext2.c
KERNEL_FS_SRC := src/kernel/fs/fs.c
EXT2_IMAGE_TOOL := tools/ext2/make_image.py
EXT2_SOURCE_DIR ?= tools/ext2/rootfs
EXT2_SIZE_MB ?=

STAGE1_BIN := $(BUILD_DIR)/boot/stage1.bin
STAGE2_OBJ := $(BUILD_DIR)/boot/stage2.o
STAGE2_C_OBJ := $(BUILD_DIR)/boot/stage2_main.o
STAGE2_OUTPUT_OBJ := $(BUILD_DIR)/boot/stage2_output.o
STAGE2_DRIVER_BLOCK_OBJ := $(BUILD_DIR)/boot/block_device.o
STAGE2_DRIVER_PARTITION_OBJ := $(BUILD_DIR)/boot/partition.o
STAGE2_DRIVER_ATA_OBJ := $(BUILD_DIR)/boot/ata.o
STAGE2_FS_EXT2_OBJ := $(BUILD_DIR)/boot/ext2.o
STAGE2_ELF := $(BUILD_DIR)/boot/stage2.elf
STAGE2_BIN := $(BUILD_DIR)/boot/stage2.bin
STAGE2_PAD := $(BUILD_DIR)/boot/stage2.padded.bin

COMMON_MEMORY_OBJ := $(BUILD_DIR)/common/memory.o
COMMON_STRING_OBJ := $(BUILD_DIR)/common/string.o
COMMON_STDIO_OBJ := $(BUILD_DIR)/common/stdio.o
COMMON_OBJS := $(COMMON_MEMORY_OBJ) $(COMMON_STRING_OBJ) $(COMMON_STDIO_OBJ)

KERNEL_MAIN_SRC      := src/kernel/main.asm
KERNEL_C_SRC         := src/kernel/kernel.c
KERNEL_OUTPUT_SRC    := src/kernel/output.c
KERNEL_PANIC_SRC     := src/kernel/panic.c
KERNEL_GDT_SRC       := src/arch/x86/cpu/gdt.c
KERNEL_GDT_FLUSH_SRC := src/arch/x86/cpu/gdt_flush.asm
KERNEL_MAIN_OBJ      := $(BUILD_DIR)/kernel/main.o
KERNEL_C_OBJ         := $(BUILD_DIR)/kernel/kernel.o
KERNEL_OUTPUT_OBJ    := $(BUILD_DIR)/kernel/output.o
KERNEL_PANIC_OBJ     := $(BUILD_DIR)/kernel/panic.o
KERNEL_GDT_OBJ       := $(BUILD_DIR)/kernel/gdt.o
KERNEL_GDT_FLUSH_OBJ := $(BUILD_DIR)/kernel/gdt_flush.o
DRIVER_BLOCK_OBJ  	 := $(BUILD_DIR)/kernel/block_device.o
DRIVER_PARTITION_OBJ := $(BUILD_DIR)/kernel/partition.o
DRIVER_ATA_OBJ    	 := $(BUILD_DIR)/kernel/ata.o
FS_EXT2_OBJ       	 := $(BUILD_DIR)/kernel/ext2.o
KERNEL_FS_OBJ     	 := $(BUILD_DIR)/kernel/fs.o
KERNEL_ELF        	 := $(BUILD_DIR)/kernel.elf
KERNEL_BIN        	 := $(BUILD_DIR)/kernel.bin

DISK_IMAGE := $(BUILD_DIR)/ginnos.img
EXT2_IMAGE := $(BUILD_DIR)/ext2.img

STAGE2_SECTORS := 62
SECTOR_SIZE    := 512
STAGE2_MAX_BYTES := $(shell echo $$(( $(STAGE2_SECTORS) * $(SECTOR_SIZE) )))

CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32
STAGE2_CFLAGS := -std=gnu11 -ffreestanding -Os -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fomit-frame-pointer
COMMON_CFLAGS := -std=gnu11 -ffreestanding -Os -Wall -Wextra -m32 -fno-pic -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fomit-frame-pointer
LDFLAGS := -T linker/kernel.ld -nostdlib
STAGE2_LDFLAGS := -T linker/stage2.ld -nostdlib --gc-sections

.PHONY: all run ext2-image run-ext2 clean check-tools

all: check-tools $(DISK_IMAGE)

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "Missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "Missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "Missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "Missing tool: $(QEMU)"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "Missing tool: $(PYTHON)"; exit 1; }

$(STAGE1_BIN): $(STAGE1_SRC)
	mkdir -p $(dir $@)
	$(AS) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512 || { echo "stage1 must be exactly 512 bytes"; exit 1; }

$(STAGE2_DRIVER_BLOCK_OBJ): $(DRIVER_BLOCK_SRC)
	mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

$(STAGE2_DRIVER_PARTITION_OBJ): $(DRIVER_PARTITION_SRC)
	mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

$(STAGE2_DRIVER_ATA_OBJ): $(DRIVER_ATA_SRC)
	mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

$(STAGE2_FS_EXT2_OBJ): $(FS_EXT2_SRC)
	mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC) $(STAGE2_C_SRC) $(STAGE2_OUTPUT_SRC) $(STAGE2_DRIVER_BLOCK_OBJ) $(STAGE2_DRIVER_PARTITION_OBJ) $(STAGE2_DRIVER_ATA_OBJ) $(STAGE2_FS_EXT2_OBJ) $(COMMON_OBJS) linker/stage2.ld
	mkdir -p $(dir $@)
	$(AS) -f elf32 $(STAGE2_SRC) -o $(STAGE2_OBJ)
	$(CC) $(STAGE2_CFLAGS) -c $(STAGE2_C_SRC) -o $(STAGE2_C_OBJ)
	$(CC) $(STAGE2_CFLAGS) -c $(STAGE2_OUTPUT_SRC) -o $(STAGE2_OUTPUT_OBJ)
	$(LD) $(STAGE2_LDFLAGS) -o $(STAGE2_ELF) $(STAGE2_OBJ) $(STAGE2_C_OBJ) $(STAGE2_OUTPUT_OBJ) $(STAGE2_DRIVER_BLOCK_OBJ) $(STAGE2_DRIVER_PARTITION_OBJ) $(STAGE2_DRIVER_ATA_OBJ) $(STAGE2_FS_EXT2_OBJ) $(COMMON_OBJS)
	$(OBJCOPY) -O binary $(STAGE2_ELF) $(STAGE2_BIN)
	@test $$(wc -c < $(STAGE2_BIN)) -le $(STAGE2_MAX_BYTES) || { echo "stage2 exceeds $(STAGE2_MAX_BYTES) bytes"; exit 1; }

$(COMMON_MEMORY_OBJ): $(COMMON_MEMORY_SRC)
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(COMMON_STRING_OBJ): $(COMMON_STRING_SRC)
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(COMMON_STDIO_OBJ): $(COMMON_STDIO_SRC)
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

$(STAGE2_PAD): $(STAGE2_BIN)
	cp $(STAGE2_BIN) $@
	truncate -s $(STAGE2_MAX_BYTES) $@

$(KERNEL_MAIN_OBJ): $(KERNEL_MAIN_SRC)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(KERNEL_C_OBJ): $(KERNEL_C_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_OUTPUT_OBJ): $(KERNEL_OUTPUT_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_PANIC_OBJ): $(KERNEL_PANIC_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_GDT_OBJ): $(KERNEL_GDT_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_GDT_FLUSH_OBJ): $(KERNEL_GDT_FLUSH_SRC)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(DRIVER_BLOCK_OBJ): $(DRIVER_BLOCK_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_PARTITION_OBJ): $(DRIVER_PARTITION_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_ATA_OBJ): $(DRIVER_ATA_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FS_EXT2_OBJ): $(FS_EXT2_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_FS_OBJ): $(KERNEL_FS_SRC)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_MAIN_OBJ) \
               $(KERNEL_C_OBJ) \
               $(KERNEL_OUTPUT_OBJ) \
               $(KERNEL_PANIC_OBJ) \
               $(KERNEL_GDT_OBJ) \
               $(KERNEL_GDT_FLUSH_OBJ) \
               $(DRIVER_BLOCK_OBJ) \
               $(DRIVER_PARTITION_OBJ) \
               $(DRIVER_ATA_OBJ) \
               $(FS_EXT2_OBJ) \
               $(KERNEL_FS_OBJ) \
               $(COMMON_OBJS) \
               linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ \
		$(KERNEL_MAIN_OBJ) \
		$(KERNEL_C_OBJ) \
		$(KERNEL_OUTPUT_OBJ) \
		$(KERNEL_PANIC_OBJ) \
		$(KERNEL_GDT_OBJ) \
		$(KERNEL_GDT_FLUSH_OBJ) \
		$(DRIVER_BLOCK_OBJ) \
		$(DRIVER_PARTITION_OBJ) \
		$(DRIVER_ATA_OBJ) \
		$(FS_EXT2_OBJ) \
		$(KERNEL_FS_OBJ) \
		$(COMMON_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@

ext2-image: $(KERNEL_BIN)
	mkdir -p $(EXT2_SOURCE_DIR)/boot
	cp $(KERNEL_BIN) $(EXT2_SOURCE_DIR)/boot/kernel.bin
	$(PYTHON) $(EXT2_IMAGE_TOOL) --source "$(EXT2_SOURCE_DIR)" --output "$(EXT2_IMAGE)" $(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)

$(DISK_IMAGE): $(STAGE1_BIN) $(STAGE2_PAD) ext2-image
	@EXT2_BYTES=$$(wc -c < $(EXT2_IMAGE)); \
	TOTAL_BYTES=$$(echo $$(( 63 * 512 + $$EXT2_BYTES ))); \
	truncate -s $$TOTAL_BYTES $@
	dd if=$(STAGE1_BIN) of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=$(STAGE2_PAD) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(EXT2_IMAGE) of=$@ bs=512 seek=63 conv=notrunc status=none

run: check-tools $(DISK_IMAGE)
	$(QEMU) -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

run-ext2: run

clean:
	rm -rf $(BUILD_DIR)
