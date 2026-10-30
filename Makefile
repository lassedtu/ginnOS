# architecture selection (prepares multi-arch builds)
ARCH ?= i686

ifeq ($(ARCH),i686)
  CROSS       ?= i686-elf-
  ARCH_DIRS   := src/arch/x86
  LINKER_DIR  := linker
  QEMU        := qemu-system-i386
  # more RAM, a full CPU model, and a large TCG translation-block cache. on an
  # Apple Silicon / arm64 host, i386 runs under TCG software emulation (no HVF
  # for i386 on ARM), so these only soften the emulation cost. override with
  # e.g. `make run QEMU_FLAGS=...`.
  QEMU_FLAGS  ?= -m 256 -cpu max -accel tcg,tb-size=256 -vga std
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
NM      := $(CROSS)nm
PYTHON  ?= python3

BUILD_DIR := build

# shared base flags
BASE_WARNINGS := -Wall -Wextra
BASE_CFLAGS   := -std=gnu11 -ffreestanding -m32 -I src -MMD -MP

# the kernel keeps a full frame-pointer chain (in release too) so panics can
# walk the stack and print a backtrace: no omitted frames, not even in leaf
# functions, and no sibling calls (a tail-call jmp would drop the caller).
KERNEL_FRAME_FLAGS := -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
                      -fno-optimize-sibling-calls

# per-subsystem compiler flags
KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -O2 -DNDEBUG \
                 -fno-pie -fno-jump-tables $(KERNEL_FRAME_FLAGS)

DRIVER_CFLAGS := $(KERNEL_CFLAGS)

COMMON_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Os \
                 -fno-pic -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -ffunction-sections -fdata-sections -fomit-frame-pointer

STAGE2_CFLAGS := $(COMMON_CFLAGS) -DEXT2_STATIC_BUFFERS

# debug build overrides (make debug)
ifeq ($(BUILD_MODE),debug)
  KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Og -g -DDEBUG \
                   -fno-pie -fno-jump-tables $(KERNEL_FRAME_FLAGS)
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

# lay out a bootable disk image: stage1 in the MBR, stage2 in sectors 1-62,
# the ext2 root filesystem from sector 63. used by the normal and test images.
# $(1) = output image, $(2) = ext2 filesystem image.
define assemble_disk
	@EXT2_BYTES=$$(wc -c < $(2)); \
	TOTAL_BYTES=$$(echo $$(( 63 * 512 + $$EXT2_BYTES ))); \
	truncate -s $$TOTAL_BYTES $(1)
	dd if=$(STAGE1_BIN) of=$(1) bs=512 seek=0 conv=notrunc status=none
	dd if=$(STAGE2_PAD) of=$(1) bs=512 seek=1 conv=notrunc status=none
	dd if=$(2) of=$(1) bs=512 seek=63 conv=notrunc status=none
endef

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
DEP_FILES := $(KERNEL_C_OBJS:.o=.d) $(COMMON_OBJS:.o=.d) $(STAGE2_C_OBJS:.o=.d) \
             $(LIBC_C_OBJS:.o=.d)

# in-kernel test build (make test). the whole kernel is recompiled into a
# separate object tree with -DKERNEL_TEST plus the tests under src/test/, so
# the normal kernel never contains test code. asm and common objects carry no
# test-specific code and are shared with the normal build.
TEST_BUILD_DIR      := $(BUILD_DIR)/test
TEST_C_SRCS         := $(call find_c_sources,src/test)
TEST_KERNEL_CFLAGS   = $(KERNEL_CFLAGS) -DKERNEL_TEST
TEST_KERNEL_C_OBJS  := $(patsubst src/%.c,$(TEST_BUILD_DIR)/obj/%.o,$(KERNEL_C_SRCS) $(TEST_C_SRCS))
TEST_KERNEL_LINK_OBJS := $(KERNEL_ENTRY_OBJ) \
                         $(filter-out $(KERNEL_ENTRY_OBJ),$(sort $(KERNEL_ASM_OBJS) $(TEST_KERNEL_C_OBJS))) \
                         $(COMMON_OBJS)
TEST_KERNEL_ELF     := $(TEST_BUILD_DIR)/kernel.elf
TEST_KERNEL_BIN     := $(TEST_BUILD_DIR)/kernel.bin
TEST_ROOTFS_DIR     := $(TEST_BUILD_DIR)/rootfs
TEST_ROOTFS_IMAGE   := $(TEST_BUILD_DIR)/rootfs.ext2
TEST_DISK_IMAGE     := $(TEST_BUILD_DIR)/ginnos-test.img
TEST_TIMEOUT        ?= 120

DEP_FILES += $(TEST_KERNEL_C_OBJS:.o=.d)

# files recording each kernel's link object list. rewritten only when the list
# changes, so adding or deleting a source file relinks the kernel (a link rule
# otherwise only notices objects that are newer than the binary, not ones that
# disappeared).
KERNEL_OBJS_LIST      := $(BUILD_DIR)/kernel.objs
TEST_KERNEL_OBJS_LIST := $(TEST_BUILD_DIR)/kernel.objs

# two-pass kernel link with an embedded symbol table (src/kernel/debug/ksyms.h).
# pass 1 links an empty table to learn the function addresses, pass 2 links the
# real table. .ksyms sits after .text, so function addresses are the same in
# both passes; the final --check fails the build if that ever stops holding.
#   $(1) output elf, $(2) link objects, $(3) scratch dir, $(4) C flags
KSYMS_TOOL := tools/gen_ksyms.py

define link_with_ksyms
	@mkdir -p $(3)
	$(PYTHON) $(KSYMS_TOOL) -o $(3)/ksyms_empty.c < /dev/null
	$(CC) $(4) -c $(3)/ksyms_empty.c -o $(3)/ksyms_empty.o
	$(LD) $(LDFLAGS) -o $(1).pass1 $(2) $(3)/ksyms_empty.o
	$(NM) -n $(1).pass1 | $(PYTHON) $(KSYMS_TOOL) -o $(3)/ksyms.c
	$(CC) $(4) -c $(3)/ksyms.c -o $(3)/ksyms.o
	$(LD) $(LDFLAGS) -o $(1) $(2) $(3)/ksyms.o
	$(NM) -n $(1) | $(PYTHON) $(KSYMS_TOOL) --check $(3)/ksyms.c
	@rm -f $(1).pass1
endef

# write $(2) into file $(1), touching it only if the content changed.
define update_if_changed
	@mkdir -p $(dir $(1))
	@echo '$(2)' > $(1).tmp; \
	if cmp -s $(1).tmp $(1); then rm -f $(1).tmp; else mv $(1).tmp $(1); fi
endef

# all source files for lint/format targets
ALL_C_SRCS := $(COMMON_C_SRCS) $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS) \
              $(STAGE2_C_SRCS) $(LIBC_C_SRCS) $(TEST_C_SRCS)
ALL_H_SRCS := $(shell find src -name '*.h' -not -path '*/zen-editor/*' 2>/dev/null | sort)

# phony targets
.PHONY: all run rootfs-image clean check-tools debug lint format compile_commands iso test test-image FORCE

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
	@command -v $(NM) >/dev/null 2>&1 || { echo "missing tool: $(NM)"; exit 1; }
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
$(KERNEL_OBJS_LIST): FORCE
	$(call update_if_changed,$@,$(KERNEL_LINK_OBJS))

$(KERNEL_ELF): $(KERNEL_LINK_OBJS) $(KERNEL_OBJS_LIST) $(LINKER_DIR)/kernel.ld $(ISR_GEN_C) $(ISR_GEN_INC) $(KSYMS_TOOL)
	$(call link_with_ksyms,$@,$(KERNEL_LINK_OBJS),$(BUILD_DIR)/ksyms,$(KERNEL_CFLAGS))

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
	$(call assemble_disk,$@,$(ROOTFS_IMAGE))

run: check-tools $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

# in-kernel tests: build the test kernel into its own disk image (a copy of the
# normal rootfs, so tests that write to the filesystem never touch
# build/ginnos.img), boot it headless, and turn QEMU's isa-debug-exit status
# into the make exit code. override the watchdog with TEST_TIMEOUT=<seconds>.
test: check-tools test-image
	./tools/run_tests.sh "$(QEMU)" "$(TEST_DISK_IMAGE)" "$(TEST_TIMEOUT)" $(QEMU_FLAGS)

$(TEST_BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_KERNEL_CFLAGS) -c $< -o $@

$(TEST_KERNEL_OBJS_LIST): FORCE
	$(call update_if_changed,$@,$(TEST_KERNEL_LINK_OBJS))

$(TEST_KERNEL_ELF): $(TEST_KERNEL_LINK_OBJS) $(TEST_KERNEL_OBJS_LIST) $(LINKER_DIR)/kernel.ld $(ISR_GEN_C) $(ISR_GEN_INC) $(KSYMS_TOOL)
	@mkdir -p $(dir $@)
	$(call link_with_ksyms,$@,$(TEST_KERNEL_LINK_OBJS),$(TEST_BUILD_DIR)/ksyms,$(TEST_KERNEL_CFLAGS))

$(TEST_KERNEL_BIN): $(TEST_KERNEL_ELF)
	$(OBJCOPY) -O binary $(TEST_KERNEL_ELF) $@

test-image: $(STAGE1_BIN) $(STAGE2_PAD) $(TEST_KERNEL_BIN) rootfs-image
	rm -rf $(TEST_ROOTFS_DIR)
	@mkdir -p $(TEST_ROOTFS_DIR)
	cp -R $(EXT2_SOURCE_DIR)/. $(TEST_ROOTFS_DIR)/
	cp $(TEST_KERNEL_BIN) $(TEST_ROOTFS_DIR)/boot/kernel.bin
	$(PYTHON) $(EXT2_IMAGE_TOOL) \
		--source "$(TEST_ROOTFS_DIR)" \
		--output "$(TEST_ROOTFS_IMAGE)" \
		--min-size-mb $(EXT2_MIN_SIZE_MB) \
		$(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)
	$(call assemble_disk,$(TEST_DISK_IMAGE),$(TEST_ROOTFS_IMAGE))

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(EXT2_SOURCE_DIR)/boot/kernel.bin
	rm -rf $(EXT2_SOURCE_DIR)/bin
	rm -f $(ISR_GEN_C) $(ISR_GEN_INC)
	rm -f compile_commands.json

# always-run prerequisite for the kernel object-list files.
FORCE:

# automatic header dependencies
-include $(DEP_FILES)
