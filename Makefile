# Toolchain

CROSS   ?= i686-elf-
AS      := nasm
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
QEMU    := qemu-system-i386
PYTHON  ?= python3

BUILD_DIR := build

#  Compiler / linker flags 

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
          -fno-pie -fno-jump-tables \
          -MMD -MP

STAGE2_CFLAGS := -std=gnu11 -ffreestanding -Os -Wall -Wextra -m32 \
                 -fno-pic -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -ffunction-sections -fdata-sections -fomit-frame-pointer \
                 -MMD -MP

COMMON_CFLAGS := -std=gnu11 -ffreestanding -Os -Wall -Wextra -m32 \
                 -fno-pic -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -ffunction-sections -fdata-sections -fomit-frame-pointer \
                 -MMD -MP

LDFLAGS        := -T linker/kernel.ld -nostdlib
STAGE2_LDFLAGS := -T linker/stage2.ld -nostdlib --gc-sections

#  Source directory sets
STAGE1_SRC := src/bootloader/stage1/boot.asm

STAGE2_C_DIRS   := src/bootloader/stage2 src/drivers/disk src/fs/ext2
STAGE2_ENTRY_ASM := src/bootloader/stage2/main.asm

KERNEL_C_DIRS   := src/kernel src/arch src/drivers src/fs
KERNEL_ASM_DIRS := src/kernel src/arch
COMMON_C_DIR    := src/common

#  ISR code generation
ISR_GEN_SCRIPT := tools/generate_isrs.sh
ISR_GEN_C      := src/arch/x86/cpu/isr_gen.c
ISR_GEN_INC    := src/arch/x86/asm/isr_gen.inc

#  Root filesystem / disk image
EXT2_IMAGE_TOOL  := tools/ext2/make_image.py
EXT2_SOURCE_DIR  ?= tools/ext2/rootfs
EXT2_SIZE_MB     ?=
EXT2_MIN_SIZE_MB ?= 8

STAGE1_BIN   := $(BUILD_DIR)/boot/stage1.bin
STAGE2_ELF   := $(BUILD_DIR)/boot/stage2.elf
STAGE2_BIN   := $(BUILD_DIR)/boot/stage2.bin
STAGE2_PAD   := $(BUILD_DIR)/boot/stage2.padded.bin
KERNEL_ELF   := $(BUILD_DIR)/kernel.elf
KERNEL_BIN   := $(BUILD_DIR)/kernel.bin
DISK_IMAGE   := $(BUILD_DIR)/ginnos.img
ROOTFS_IMAGE := $(BUILD_DIR)/images/rootfs.ext2

STAGE2_SECTORS   := 62
SECTOR_SIZE      := 512
STAGE2_MAX_BYTES := $(shell echo $$(( $(STAGE2_SECTORS) * $(SECTOR_SIZE) )))

#  Path mapping helpers 
src_c_to_obj      = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(1))
src_asm_to_obj    = $(patsubst src/%.asm,$(BUILD_DIR)/%.o,$(1))
src_c_to_stage2   = $(patsubst src/%.c,$(BUILD_DIR)/stage2/%.o,$(1))

find_c_sources    = $(shell find $(1) -name '*.c' 2>/dev/null | sort)
find_asm_sources  = $(shell find $(1) -name '*.asm' 2>/dev/null | sort)

#  Automatic source discovery
COMMON_C_SRCS := $(call find_c_sources,$(COMMON_C_DIR))
COMMON_OBJS   := $(call src_c_to_obj,$(COMMON_C_SRCS))

STAGE2_C_SRCS    := $(foreach d,$(STAGE2_C_DIRS),$(call find_c_sources,$(d)))
STAGE2_C_OBJS    := $(call src_c_to_stage2,$(STAGE2_C_SRCS))
STAGE2_ENTRY_OBJ := $(BUILD_DIR)/stage2/bootloader/stage2/entry.o

# Generated ISR table
KERNEL_C_SRCS := $(sort $(foreach d,$(KERNEL_C_DIRS),$(call find_c_sources,$(d))) \
                         $(ISR_GEN_C))
KERNEL_ASM_SRCS := $(foreach d,$(KERNEL_ASM_DIRS),$(call find_asm_sources,$(d)))

KERNEL_C_OBJS   := $(call src_c_to_obj,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(call src_asm_to_obj,$(KERNEL_ASM_SRCS))
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/kernel/main.o

# Link order
KERNEL_LINK_OBJS := $(KERNEL_ENTRY_OBJ) \
                    $(filter-out $(KERNEL_ENTRY_OBJ),$(sort $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS))) \
                    $(COMMON_OBJS)

STAGE2_LINK_OBJS := $(STAGE2_ENTRY_OBJ) $(sort $(STAGE2_C_OBJS)) $(COMMON_OBJS)

# Dependency files (C compilations only)
DEP_FILES := $(KERNEL_C_OBJS:.o=.d) $(COMMON_OBJS:.o=.d) $(STAGE2_C_OBJS:.o=.d)

# Phony targets
.PHONY: all run rootfs-image clean check-tools

all: check-tools $(DISK_IMAGE)

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "Missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "Missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "Missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "Missing tool: $(QEMU)"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "Missing tool: $(PYTHON)"; exit 1; }

# ISR generation
$(ISR_GEN_C) $(ISR_GEN_INC): $(ISR_GEN_SCRIPT)
	@mkdir -p $(dir $(ISR_GEN_C)) $(dir $(ISR_GEN_INC))
	./$(ISR_GEN_SCRIPT) $(ISR_GEN_C) $(ISR_GEN_INC)

# Stage 1 bootloader flat binary, no C
$(STAGE1_BIN): $(STAGE1_SRC)
	@mkdir -p $(dir $@)
	$(AS) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512 || { echo "stage1 must be exactly 512 bytes"; exit 1; }

# Pattern rules shared library code (compiled once, linked into stage2 + kernel)
$(BUILD_DIR)/common/%.o: src/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@


# Pattern rules kernel (C + NASM elf32)
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -I $(dir $<) $< -o $@

# isr.asm %includes the generated stub table
$(BUILD_DIR)/arch/x86/asm/isr.o: $(ISR_GEN_INC)


# Pattern rules stage2 (separate object tree, different C flags)
$(BUILD_DIR)/stage2/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

# main.asm is the stage2 entry point; main.c lives in the same directory,
# so the entry object is named entry.o to avoid a basename collision.
$(STAGE2_ENTRY_OBJ): $(STAGE2_ENTRY_ASM)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# Stage 2 link
$(STAGE2_BIN): $(STAGE2_LINK_OBJS) linker/stage2.ld $(ISR_GEN_SCRIPT)
	@mkdir -p $(dir $(STAGE2_ELF))
	$(LD) $(STAGE2_LDFLAGS) -o $(STAGE2_ELF) $(STAGE2_LINK_OBJS)
	$(OBJCOPY) -O binary $(STAGE2_ELF) $@
	@test $$(wc -c < $@) -le $(STAGE2_MAX_BYTES) || \
		{ echo "stage2 exceeds $(STAGE2_MAX_BYTES) bytes"; exit 1; }

$(STAGE2_PAD): $(STAGE2_BIN)
	cp $(STAGE2_BIN) $@
	truncate -s $(STAGE2_MAX_BYTES) $@

# Kernel link
$(KERNEL_ELF): $(KERNEL_LINK_OBJS) linker/kernel.ld $(ISR_GEN_C) $(ISR_GEN_INC)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_LINK_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@

# Root filesystem and disk image
rootfs-image: $(KERNEL_BIN)
	@mkdir -p $(EXT2_SOURCE_DIR)/boot $(dir $(ROOTFS_IMAGE))
	cp $(KERNEL_BIN) $(EXT2_SOURCE_DIR)/boot/kernel.bin
	$(PYTHON) $(EXT2_IMAGE_TOOL) \
		--source "$(EXT2_SOURCE_DIR)" \
		--output "$(ROOTFS_IMAGE)" \
		--min-size-mb $(EXT2_MIN_SIZE_MB) \
		$(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)

$(DISK_IMAGE): $(STAGE1_BIN) $(STAGE2_PAD) rootfs-image
	@EXT2_BYTES=$$(wc -c < $(ROOTFS_IMAGE)); \
	TOTAL_BYTES=$$(echo $$(( 63 * 512 + $$EXT2_BYTES ))); \
	truncate -s $$TOTAL_BYTES $@
	dd if=$(STAGE1_BIN) of=$@ bs=512 seek=0 conv=notrunc status=none
	dd if=$(STAGE2_PAD) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(ROOTFS_IMAGE) of=$@ bs=512 seek=63 conv=notrunc status=none

run: check-tools $(DISK_IMAGE)
	$(QEMU) -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(EXT2_SOURCE_DIR)/boot/kernel.bin
	rm -f $(ISR_GEN_C) $(ISR_GEN_INC)

# Automatic header dependencies
-include $(DEP_FILES)
