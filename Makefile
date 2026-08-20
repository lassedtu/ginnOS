# architecture selection (prepares multi-arch builds)
ARCH ?= i686

ifeq ($(ARCH),i686)
  CROSS       ?= i686-elf-
  ARCH_DIRS   := src/arch/x86
  LINKER_DIR  := linker
  QEMU        := qemu-system-i386
  ASM_FORMAT  := elf32
  ASM_BIN_FMT := bin
else
  $(error unsupported ARCH=$(ARCH). currently only i686 is implemented)
endif

# toolchain
AS      := nasm
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
PYTHON  ?= python3

BUILD_DIR := build

# shared base flags
BASE_WARNINGS := -Wall -Wextra
BASE_CFLAGS   := -std=gnu11 -ffreestanding -m32 -I src -MMD -MP

# per-subsystem compiler flags
KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -O2 -DNDEBUG \
                 -fno-pie -fno-jump-tables

DRIVER_CFLAGS := $(KERNEL_CFLAGS)

COMMON_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Os \
                 -fno-pic -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -ffunction-sections -fdata-sections -fomit-frame-pointer

STAGE2_CFLAGS := $(COMMON_CFLAGS)

# debug build overrides (make debug)
ifeq ($(BUILD_MODE),debug)
  KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Og -g -DDEBUG \
                   -fno-pie -fno-jump-tables
  DRIVER_CFLAGS := $(KERNEL_CFLAGS)
endif

# linker flags
LDFLAGS        := -T $(LINKER_DIR)/kernel.ld -nostdlib
STAGE2_LDFLAGS := -T $(LINKER_DIR)/stage2.ld -nostdlib --gc-sections

# source directory sets
STAGE1_SRC := src/bootloader/stage1/boot.asm

STAGE2_C_DIRS    := src/bootloader/stage2 src/drivers/disk src/fs/ext2
STAGE2_ENTRY_ASM := src/bootloader/stage2/main.asm

KERNEL_C_DIRS   := src/kernel $(ARCH_DIRS) src/drivers src/fs
KERNEL_ASM_DIRS := src/kernel $(ARCH_DIRS)
COMMON_C_DIR    := src/common

# isr code generation
ISR_GEN_SCRIPT := tools/generate_isrs.sh
ISR_GEN_C      := src/arch/x86/cpu/isr_gen.c
ISR_GEN_INC    := src/arch/x86/asm/isr_gen.inc

# root filesystem / disk image
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

# path mapping helpers
src_c_to_obj      = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(1))
src_asm_to_obj    = $(patsubst src/%.asm,$(BUILD_DIR)/%.o,$(1))
src_c_to_stage2   = $(patsubst src/%.c,$(BUILD_DIR)/stage2/%.o,$(1))

find_c_sources    = $(shell find $(1) -name '*.c' 2>/dev/null | sort)
find_asm_sources  = $(shell find $(1) -name '*.asm' 2>/dev/null | sort)

# automatic source discovery
COMMON_C_SRCS := $(call find_c_sources,$(COMMON_C_DIR))
COMMON_OBJS   := $(call src_c_to_obj,$(COMMON_C_SRCS))

STAGE2_C_SRCS    := $(foreach d,$(STAGE2_C_DIRS),$(call find_c_sources,$(d)))
STAGE2_C_OBJS    := $(call src_c_to_stage2,$(STAGE2_C_SRCS))
STAGE2_ENTRY_OBJ := $(BUILD_DIR)/stage2/bootloader/stage2/entry.o

# generated isr table
KERNEL_C_SRCS := $(sort $(foreach d,$(KERNEL_C_DIRS),$(call find_c_sources,$(d))) \
                         $(ISR_GEN_C))
KERNEL_ASM_SRCS := $(foreach d,$(KERNEL_ASM_DIRS),$(call find_asm_sources,$(d)))

KERNEL_C_OBJS   := $(call src_c_to_obj,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(call src_asm_to_obj,$(KERNEL_ASM_SRCS))
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/kernel/main.o

# link order
KERNEL_LINK_OBJS := $(KERNEL_ENTRY_OBJ) \
                    $(filter-out $(KERNEL_ENTRY_OBJ),$(sort $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS))) \
                    $(COMMON_OBJS)

STAGE2_LINK_OBJS := $(STAGE2_ENTRY_OBJ) $(sort $(STAGE2_C_OBJS)) $(COMMON_OBJS)

# user programs
USER_CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
                -fno-pie -fno-stack-protector -nostdlib \
                -Isrc/libc/include -MMD -MP
USER_LDFLAGS := -T $(LINKER_DIR)/user.ld -nostdlib
USER_CRT0    := $(BUILD_DIR)/user/lib/crt0.o

# libc
LIBC_CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
                -fno-pie -fno-stack-protector -Isrc/libc/include -MMD -MP
LIBC_C_SRCS  := $(shell find src/libc/src -name '*.c' 2>/dev/null | sort)
LIBC_ASM_SRCS := $(shell find src/libc/src -name '*.asm' 2>/dev/null | sort)
LIBC_C_OBJS  := $(patsubst src/libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_C_SRCS))
LIBC_ASM_OBJS := $(patsubst src/libc/src/%.asm,$(BUILD_DIR)/libc/%.o,$(LIBC_ASM_SRCS))
LIBC_OBJS    := $(LIBC_C_OBJS) $(LIBC_ASM_OBJS)
LIBC_A       := $(BUILD_DIR)/libc/libc.a

# user programs: automatic discovery via per-program Makefiles.
# each program lives in src/user/<name>/ with its own Makefile.
# src/user/lib/ is excluded (it contains crt0, not a program).
USER_PROG_DIRS := $(sort $(filter-out src/user/lib,$(patsubst %/Makefile,%,$(wildcard src/user/*/Makefile))))
USER_PROG_NAMES := $(notdir $(USER_PROG_DIRS))
USER_PROGRAMS := $(addprefix $(BUILD_DIR)/user/bin/,$(USER_PROG_NAMES))

# dependency files (C compilations only)
DEP_FILES := $(KERNEL_C_OBJS:.o=.d) $(COMMON_OBJS:.o=.d) $(STAGE2_C_OBJS:.o=.d)

# all source files for lint/format targets
ALL_C_SRCS := $(COMMON_C_SRCS) $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS) \
              $(STAGE2_C_SRCS) $(LIBC_C_SRCS)
ALL_H_SRCS := $(shell find src -name '*.h' -not -path '*/zen-editor/*' 2>/dev/null | sort)

# phony targets
.PHONY: all run rootfs-image clean check-tools debug lint format compile_commands iso

all: check-tools $(DISK_IMAGE)

# debug build: compile with -Og -g -DDEBUG and launch qemu with gdb stub
debug:
	$(MAKE) BUILD_MODE=debug all
	$(QEMU) -s -S -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE) &
	@echo "qemu started with gdb stub on localhost:1234"
	@echo "connect with: gdb build/kernel.elf -ex 'target remote :1234'"

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "missing tool: $(QEMU)"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "missing tool: $(PYTHON)"; exit 1; }

# static analysis via clang-tidy (uses .clang-tidy config)
lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "install clang-tidy to use make lint"; exit 1; }
	clang-tidy $(filter %.c,$(ALL_C_SRCS)) $(ALL_H_SRCS) -- $(KERNEL_CFLAGS)

# auto-format via clang-format (uses .clang-format config)
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "install clang-format to use make format"; exit 1; }
	clang-format -i $(filter %.c,$(ALL_C_SRCS)) $(ALL_H_SRCS)

# generate compile_commands.json for clangd/lsp support.
# uses a simple python script to build from the Makefile's dry-run output.
compile_commands:
	@command -v bear >/dev/null 2>&1 && { \
		$(MAKE) clean; \
		bear -- $(MAKE) all; \
	} || { \
		echo '[]' > compile_commands.json; \
		$(MAKE) clean; \
		$(MAKE) all CC_JSON=1 2>&1 | grep '$(CC)' | \
		$(PYTHON) -c "\
import sys, json, os; \
entries = []; \
for line in sys.stdin: \
    parts = line.strip().split(); \
    if not parts: continue; \
    src = next((p for p in parts if p.endswith('.c')), None); \
    if src: entries.append({'directory': os.getcwd(), 'command': line.strip(), 'file': src}); \
json.dump(entries, open('compile_commands.json','w'), indent=2)"; \
		echo "generated compile_commands.json"; \
	}

# grub-bootable iso image (requires grub-mkrescue and xorriso)
iso: check-tools $(KERNEL_ELF)
	@command -v grub-mkrescue >/dev/null 2>&1 || { echo "install grub-mkrescue to use make iso"; exit 1; }
	@mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(KERNEL_ELF) $(BUILD_DIR)/isodir/boot/kernel.elf
	echo 'menuentry "ginnOS" { multiboot /boot/kernel.elf }' > $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/ginnos.iso $(BUILD_DIR)/isodir
	@echo "iso created: $(BUILD_DIR)/ginnos.iso"

# isr generation
$(ISR_GEN_C) $(ISR_GEN_INC): $(ISR_GEN_SCRIPT)
	@mkdir -p $(dir $(ISR_GEN_C)) $(dir $(ISR_GEN_INC))
	./$(ISR_GEN_SCRIPT) $(ISR_GEN_C) $(ISR_GEN_INC)

# stage 1 bootloader flat binary, no C
$(STAGE1_BIN): $(STAGE1_SRC)
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_BIN_FMT) $< -o $@
	@test $$(wc -c < $@) -eq 512 || { echo "stage1 must be exactly 512 bytes"; exit 1; }

# pattern rules: shared library code (compiled once, linked into stage2 + kernel)
$(BUILD_DIR)/common/%.o: src/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

# pattern rules: kernel (C + nasm elf32)
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) -I $(dir $<) $< -o $@

# isr.asm %includes the generated stub table
$(BUILD_DIR)/arch/x86/asm/isr.o: $(ISR_GEN_INC)

# pattern rules: stage2 (separate object tree, different C flags)
$(BUILD_DIR)/stage2/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

# main.asm is the stage2 entry point; main.c lives in the same directory,
# so the entry object is named entry.o to avoid a basename collision.
$(STAGE2_ENTRY_OBJ): $(STAGE2_ENTRY_ASM)
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

# stage 2 link
$(STAGE2_BIN): $(STAGE2_LINK_OBJS) $(LINKER_DIR)/stage2.ld $(ISR_GEN_SCRIPT)
	@mkdir -p $(dir $(STAGE2_ELF))
	$(LD) $(STAGE2_LDFLAGS) -o $(STAGE2_ELF) $(STAGE2_LINK_OBJS)
	$(OBJCOPY) -O binary $(STAGE2_ELF) $@
	@test $$(wc -c < $@) -le $(STAGE2_MAX_BYTES) || \
		{ echo "stage2 exceeds $(STAGE2_MAX_BYTES) bytes"; exit 1; }

$(STAGE2_PAD): $(STAGE2_BIN)
	cp $(STAGE2_BIN) $@
	truncate -s $(STAGE2_MAX_BYTES) $@

# kernel link
$(KERNEL_ELF): $(KERNEL_LINK_OBJS) $(LINKER_DIR)/kernel.ld $(ISR_GEN_C) $(ISR_GEN_INC)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_LINK_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@

# libc build rules
$(BUILD_DIR)/libc/%.o: src/libc/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/%.o: src/libc/src/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

$(LIBC_A): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(CROSS)ar rcs $@ $(LIBC_OBJS)

# user program build rules
$(USER_CRT0): src/user/lib/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

# delegate to per-program Makefiles
user-programs: $(USER_CRT0) $(LIBC_A)
	@for d in $(USER_PROG_DIRS); do \
		$(MAKE) --no-print-directory -C $$d || exit 1; \
	done

# root filesystem and disk image
rootfs-image: $(KERNEL_BIN) user-programs
	@mkdir -p $(EXT2_SOURCE_DIR)/boot $(EXT2_SOURCE_DIR)/bin $(dir $(ROOTFS_IMAGE))
	cp $(KERNEL_BIN) $(EXT2_SOURCE_DIR)/boot/kernel.bin
	@for prog in $(USER_PROGRAMS); do \
		cp $$prog $(EXT2_SOURCE_DIR)/bin/$$(basename $$prog); \
	done
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
	rm -rf $(EXT2_SOURCE_DIR)/bin
	rm -f $(ISR_GEN_C) $(ISR_GEN_INC)
	rm -f compile_commands.json

# automatic header dependencies
-include $(DEP_FILES)
