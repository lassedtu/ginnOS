# architecture selection (prepares multi-arch builds)
ARCH ?= i686

ifeq ($(ARCH),i686)
  CROSS       ?= i686-elf-
  ARCH_DIRS   := src/arch/x86
  LINKER_DIR  := linker
  BUILD_DIR   := build
  ARCH_CFLAGS := -m32
  # the kernel and the C code linked into it
  KERNEL_ARCH_CFLAGS :=
  QEMU        := qemu-system-i386
  # more RAM, a full CPU model, and a large TCG translation-block cache. on an
  # Apple Silicon / arm64 host, i386 runs under TCG software emulation (no HVF
  # for i386 on ARM), so these only soften the emulation cost. override with
  # e.g. `make run QEMU_FLAGS=...`.
  QEMU_FLAGS  ?= -m 512 -cpu max -accel tcg,tb-size=256 $(QEMU_VGA_FLAGS)
  ASM_FORMAT  := elf32
  ASM_BIN_FMT := bin
else ifeq ($(ARCH),x86_64)
  CROSS       ?= x86_64-elf-
  ARCH_DIRS   := src/arch/x86
  LINKER_DIR  := linker/x86_64
  BUILD_DIR   := build/x86_64
  ARCH_CFLAGS := -m64
  # the kernel is linked in the top 2 GiB, must keep the red zone free
  # (interrupts push onto the kernel stack) and must not use SSE (its
  # registers are the user program's)
  KERNEL_ARCH_CFLAGS := -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
  QEMU        := qemu-system-x86_64
  QEMU_FLAGS  ?= -m 512 -cpu max -accel tcg,tb-size=256 $(QEMU_VGA_FLAGS)
  ASM_FORMAT  := elf64
  ASM_BIN_FMT := bin
  ifneq ($(BOOTLOADER),)
    ifneq ($(BOOTLOADER),limine)
      $(error ARCH=x86_64 boots only with BOOTLOADER=limine)
    endif
  endif
else
  $(error unsupported ARCH=$(ARCH): use i686 or x86_64)
endif

# toolchain
AS      := nasm
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NM      := $(CROSS)nm
PYTHON  ?= python3
GDB     ?= gdb
GDB_PORT ?= 1234

# extra flags the cross toolchain needs (tools/toolchain_flags.sh): for a
# gcc 16 with binutils older than 2.43, "-B <dir>" for newer binutils from
# homebrew. set TOOLCHAIN_FLAGS= to turn the probe off.
TOOLCHAIN_FLAGS ?= $(shell tools/toolchain_flags.sh $(CC) $(patsubst %-,%,$(CROSS)))
TOOLCHAIN_FLAGS := $(TOOLCHAIN_FLAGS)

# shared base flags
BASE_WARNINGS := -Wall -Wextra
BASE_CFLAGS   := -std=gnu11 -ffreestanding $(ARCH_CFLAGS) $(TOOLCHAIN_FLAGS) -I src -MMD -MP

# the kernel keeps a full frame-pointer chain (in release too) so panics can
# walk the stack and print a backtrace: no omitted frames, not even in leaf
# functions, and no sibling calls (a tail-call jmp would drop the caller).
KERNEL_FRAME_FLAGS := -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
                      -fno-optimize-sibling-calls

# per-subsystem compiler flags. -g only adds debug info to the ELF files (for
# make gdb); the flat binaries made with objcopy are unchanged by it.
KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -O2 -g -DNDEBUG \
                 -fno-pie -fno-jump-tables $(KERNEL_FRAME_FLAGS) $(KERNEL_ARCH_CFLAGS) \
                 -I$(BUILD_DIR)/generated

# the release of ginnOS (uname -r). the version (uname -v) is the git
# commit and its date, in build/<arch>/generated/version.h.
GINNOS_RELEASE := 0.1

DRIVER_CFLAGS := $(KERNEL_CFLAGS)

COMMON_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Os -g \
                 -fno-pic -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -ffunction-sections -fdata-sections -fomit-frame-pointer \
                 $(KERNEL_ARCH_CFLAGS)

STAGE2_CFLAGS := $(COMMON_CFLAGS) -DEXT2_STATIC_BUFFERS

# debug build overrides (make debug). switching modes rebuilds the kernel
# objects, because they depend on $(KERNEL_FLAGS_FILE) (see below).
ifeq ($(BUILD_MODE),debug)
  KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Og -g -DDEBUG \
                   -fno-pie -fno-jump-tables $(KERNEL_FRAME_FLAGS) $(KERNEL_ARCH_CFLAGS)
  DRIVER_CFLAGS := $(KERNEL_CFLAGS)
endif

# linker flags
LDFLAGS        := -T $(LINKER_DIR)/kernel.ld -nostdlib -z max-page-size=0x1000
STAGE2_LDFLAGS := -T $(LINKER_DIR)/stage2.ld -nostdlib --gc-sections

# source directory sets
STAGE1_SRC := src/bootloader/stage1/boot.asm

STAGE2_C_DIRS    := src/bootloader/stage2 src/drivers/disk src/fs/ext2
STAGE2_ENTRY_ASM := src/bootloader/stage2/main.asm

KERNEL_C_DIRS   := src/kernel $(ARCH_DIRS) src/drivers src/fs
COMMON_C_DIR    := src/common

# isr code generation
ISR_GEN_SCRIPT := tools/generate_isrs.sh
ISR_GEN_C      := src/arch/x86/cpu/isr_gen.c
ISR_GEN_INC    := src/arch/x86/asm/isr_gen.inc

# root filesystem / disk image
EXT2_IMAGE_TOOL  := tools/ext2/make_image.py
EXT2_SOURCE_DIR  ?= tools/ext2/rootfs
# the files of the root filesystem: a copy of EXT2_SOURCE_DIR with the
# programs (and, for stage2, kernel.bin), one per architecture
ROOTFS_STAGING   := $(BUILD_DIR)/rootfs
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

# the bootloader of the disk image:
#   limine (default): limine on BIOS and UEFI. a FAT boot partition has
#     limine, the kernel ELF (multiboot 2) and the root image as a module; the
#     ext2 root is partition 2. needs git (once, to fetch limine) and mtools.
#   custom: the old BIOS-only stage1 and stage2 (src/bootloader), which load
#     kernel.bin from the ext2 root.
BOOTLOADER ?= limine

# GPT=1 makes a GPT disk (with a BIOS boot partition for limine's BIOS
# stage) instead of an MBR disk; limine only
GPT ?=

# DOOM=1 builds /bin/doom (src/user/doom) and puts the Freedoom game data
# (freedoom1.wad, about 28 MiB) into /usr/share/doom. the data is fetched once
# into build/freedoom (a pinned, checksum-checked release). the default (DOOM=0)
# leaves both out. DOOM=1 FREEDOOM=0 builds the program without the data.
DOOM     ?= 0
FREEDOOM ?= $(DOOM)
# with DOOM=1, every .wad file in DOOM_WAD_DIR (your own game data, for
# example the Doom shareware doom1.wad; git ignores .hidden) goes into
# /usr/share/doom too, with a lowercase name. doom picks the first one of
# doom2.wad, plutonia.wad, tnt.wad, doom.wad, doom1.wad, freedoom2.wad and
# freedoom1.wad it finds; doom -iwad <file> picks another.
DOOM_WAD_DIR ?= .hidden
FREEDOOM_FETCH := tools/freedoom/fetch.sh
FREEDOOM_CACHE := build/freedoom

# the TrueType fonts for bragi (Inter and JetBrains Mono, SIL OFL 1.1) go
# into /usr/share/fonts. tools/fonts/fetch.sh downloads the pinned
# releases once into build/fonts (kept by make clean, like Freedoom).
# FONTS=0 leaves them out.
FONTS ?= 1
FONTS_FETCH := tools/fonts/fetch.sh
FONTS_CACHE := build/fonts

# the icon packs (Lucide, ISC, and Tela, GPL-3.0) go into /usr/share/icons.
# tools/icons/fetch.sh downloads the pinned releases into build/icons (kept
# by make clean), and tools/icons/build.py draws the listed icons into the
# packs with rsvg-convert or resvg. ICONS=0 leaves them out.
ICONS ?= 1
ICONS_CACHE := build/icons
ICONS_OUT   := $(ICONS_CACHE)/out
ICONS_PACKS := $(ICONS_OUT)/lucide.pack $(ICONS_OUT)/tela.pack

# the default wallpaper (/usr/share/wallpapers/default.png), made by
# tools/wallpaper/make.py from your own image (git ignores .hidden): put on
# black at the screen size (RESOLUTION), so it is shown pixel for pixel.
# needs Pillow; without the image or Pillow, heimdalld draws its gradient.
# WALLPAPER= leaves it out.
WALLPAPER     ?= .hidden/orbitals/6_3_1.png
WALLPAPER_OUT  = build/wallpaper/default-$(or $(RESOLUTION),any).png

LIMINE_DIR       := build/limine
LIMINE_FETCH     := tools/limine/fetch.sh
LIMINE_DISK_TOOL := tools/limine/make_disk.py
LIMINE_CONF      := tools/limine/limine-$(ARCH).conf
# the screen size, for example RESOLUTION=1280x800. empty: limine picks the
# size (in QEMU: 1280x1024 for i686, 1280x800 for x86_64).
RESOLUTION       ?= 1440x900
# the graphics card: virtio-vga. its window opens at RESOLUTION; when the
# window is resized (or goes full screen), the desktop takes the new size.
# QEMU_VGA=std gives the plain VGA card, fixed at RESOLUTION.
QEMU_VGA         ?= virtio
VGA_SIZE          = $(subst x, ,$(RESOLUTION))
QEMU_VGA_FLAGS    = -vga $(QEMU_VGA) \
                    $(if $(and $(filter virtio,$(QEMU_VGA)),$(RESOLUTION)), \
                      -global virtio-vga.xres=$(word 1,$(VGA_SIZE)) \
                      -global virtio-vga.yres=$(word 2,$(VGA_SIZE)))

# UEFI firmware for make run-uefi (OVMF; homebrew and nix qemu include it)
QEMU_UEFI ?= qemu-system-x86_64
OVMF_CODE ?= $(firstword $(wildcard \
	/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
	/usr/local/share/qemu/edk2-x86_64-code.fd \
	/usr/share/qemu/edk2-x86_64-code.fd \
	/usr/share/OVMF/OVMF_CODE.fd \
	/usr/share/edk2/x64/OVMF_CODE.fd))

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

# lay out a limine disk image (tools/limine/make_disk.py). the FAT gets a
# copy of the kernel ELF without debug information.
# $(1) = output image, $(2) = kernel ELF, $(3) = ext2 filesystem image,
# $(4) = scratch directory.
define limine_disk
	./$(LIMINE_FETCH) $(LIMINE_DIR)
	@mkdir -p $(4)
	$(OBJCOPY) --strip-debug $(2) $(4)/kernel.elf
	cp $(LIMINE_CONF) $(4)/limine.conf
	$(if $(RESOLUTION),printf '    resolution: %s\n' '$(RESOLUTION)' >> $(4)/limine.conf)
	$(PYTHON) $(LIMINE_DISK_TOOL) --output $(1) --ext2 $(3) --limine-dir $(LIMINE_DIR) $(if $(filter 1,$(GPT)),--gpt) \
		--file $(4)/limine.conf:/boot/limine/limine.conf \
		--file $(LIMINE_DIR)/limine-bios.sys:/boot/limine/limine-bios.sys \
		--file $(LIMINE_DIR)/BOOTX64.EFI:/EFI/BOOT/BOOTX64.EFI \
		--file $(LIMINE_DIR)/BOOTIA32.EFI:/EFI/BOOT/BOOTIA32.EFI \
		--file $(4)/kernel.elf:/boot/kernel.elf \
		--file $(3):/boot/rootfs.ext2
endef

ifneq ($(BOOTLOADER),limine)
  ifneq ($(BOOTLOADER),custom)
    $(error unsupported BOOTLOADER=$(BOOTLOADER): use limine or custom)
  endif
endif

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
# the 64-bit assembly is in src/arch/x86/asm64; the rest is 32-bit
ifeq ($(ARCH),x86_64)
KERNEL_ASM_SRCS := $(call find_asm_sources,src/arch/x86/asm64)
else
KERNEL_ASM_SRCS := $(filter-out src/arch/x86/asm64/%,$(call find_asm_sources,src/kernel src/arch/x86))
endif

KERNEL_C_OBJS   := $(call src_c_to_obj,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(call src_asm_to_obj,$(KERNEL_ASM_SRCS))
ifeq ($(ARCH),x86_64)
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/arch/x86/asm64/entry.o
else
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/kernel/main.o
endif

# link order
KERNEL_LINK_OBJS := $(KERNEL_ENTRY_OBJ) \
                    $(filter-out $(KERNEL_ENTRY_OBJ),$(sort $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS))) \
                    $(COMMON_OBJS)

STAGE2_LINK_OBJS := $(STAGE2_ENTRY_OBJ) $(sort $(STAGE2_C_OBJS)) $(COMMON_OBJS)

# user programs
USER_CRT0    := $(BUILD_DIR)/user/lib/crt0.o
USER_CRT0_SRC := src/user/lib/$(ARCH)/crt0.asm
USER_CRTI    := $(BUILD_DIR)/user/lib/crti.o
USER_CRTN    := $(BUILD_DIR)/user/lib/crtn.o

# the sysroot of the ginnOS toolchain (tools/toolchain): what
# <arch>-ginnos-gcc finds as /usr/include and /usr/lib
SYSROOT      := $(abspath $(BUILD_DIR))/sysroot

# libc
LIBC_CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra $(ARCH_CFLAGS) $(TOOLCHAIN_FLAGS) \
                -fno-pie -fno-stack-protector -Isrc/libc/include -MMD -MP
LIBC_C_SRCS  := $(shell find src/libc/src -name '*.c' 2>/dev/null | sort)
LIBC_ASM_SRCS := $(shell find src/libc/arch/$(ARCH) -name '*.asm' 2>/dev/null | sort)
LIBC_C_OBJS  := $(patsubst src/libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_C_SRCS))
LIBC_ASM_OBJS := $(patsubst src/libc/arch/$(ARCH)/%.asm,$(BUILD_DIR)/libc/arch/%.o,$(LIBC_ASM_SRCS))
LIBC_OBJS    := $(LIBC_C_OBJS) $(LIBC_ASM_OBJS)
LIBC_A       := $(BUILD_DIR)/libc/libc.a

# user programs: automatic discovery via per-program Makefiles.
# each program lives in src/user/<name>/ with its own Makefile.
# src/user/lib/ is excluded (it contains crt0, not a program).
# src/user/doom is built only with DOOM=1.
USER_PROG_DIRS := $(sort $(filter-out src/user/lib $(if $(filter 1,$(DOOM)),,src/user/doom),\
                  $(patsubst %/Makefile,%,$(wildcard src/user/*/Makefile))))
USER_PROG_NAMES := $(notdir $(USER_PROG_DIRS))
USER_PROGRAMS := $(addprefix $(BUILD_DIR)/user/bin/,$(USER_PROG_NAMES))

# what the per-program Makefiles (src/user/program.mk) need to know
USER_MAKE_VARS := CROSS=$(CROSS) BUILD_ROOT=$(abspath $(BUILD_DIR)) \
                  ARCH_CFLAGS=$(ARCH_CFLAGS) USER_LD_SCRIPT=$(abspath $(LINKER_DIR)/user.ld) \
                  TOOLCHAIN_FLAGS="$(TOOLCHAIN_FLAGS)"

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

# file recording the kernel C flags, rewritten only when they change, so
# kernel objects rebuild when the flags do (e.g. make debug after make all).
KERNEL_FLAGS_FILE := $(BUILD_DIR)/kernel.flags

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
.PHONY: sysroot all run run-uefi rootfs-image clean distclean check-tools debug gdb lint check64 format compile_commands test test-image FORCE user-libs host-test icons

all: check-tools $(DISK_IMAGE)

# debug the kernel: boot QEMU halted at the first instruction with a gdb stub
# on GDB_PORT, attach gdb with build/kernel.elf and tools/gdb/ginnos.gdb
# (breakpoints on panics), and stop QEMU when gdb exits. type `c` to boot.
gdb: check-tools $(DISK_IMAGE)
	@command -v $(GDB) >/dev/null 2>&1 || { echo "missing tool: $(GDB) (set GDB=...)"; exit 1; }
	@PYTHON="$(PYTHON)" ./tools/gdb/run_gdb.sh "$(QEMU)" "$(DISK_IMAGE)" "$(KERNEL_ELF)" \
		"$(GDB)" "$(GDB_PORT)" $(QEMU_FLAGS) -- $(GDB_ARGS)

# same as make gdb, but with the kernel built -Og -DDEBUG (easier stepping,
# fewer optimised-out variables, debug-level klog).
debug:
	$(MAKE) BUILD_MODE=debug gdb

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(NM) >/dev/null 2>&1 || { echo "missing tool: $(NM)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "missing tool: $(QEMU)"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "missing tool: $(PYTHON)"; exit 1; }
ifeq ($(BOOTLOADER),limine)
	@command -v mformat >/dev/null 2>&1 || { echo "missing tool: mformat (install mtools)"; exit 1; }
	@command -v git >/dev/null 2>&1 || { echo "missing tool: git (to fetch limine)"; exit 1; }
endif

# static analysis via clang-tidy (uses .clang-tidy config)
lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "install clang-tidy to use make lint"; exit 1; }
	clang-tidy $(filter %.c,$(ALL_C_SRCS)) $(ALL_H_SRCS) -- $(KERNEL_CFLAGS)

# x86_64 readiness check: a syntax-only x86_64 compile (host clang) of the
# kernel, drivers, fs, common code and tests. any pointer/integer cast,
# 64-to-32-bit narrowing or printf format mismatch is an error, so code that
# only works because pointers are 32 bits is caught before the x86_64 port.
CLANG ?= clang
CHECK64_FLAGS := --target=x86_64-elf -ffreestanding -fsyntax-only -std=gnu11 -I src \
                 -DKERNEL_TEST -Wno-everything -Werror -Wpointer-to-int-cast \
                 -Wint-to-pointer-cast -Wvoid-pointer-to-int-cast -Wshorten-64-to-32 \
                 -Wformat

check64:
	@command -v $(CLANG) >/dev/null 2>&1 || { echo "install clang to use make check64"; exit 1; }
	@for f in $(KERNEL_C_SRCS) $(COMMON_C_SRCS) $(TEST_C_SRCS); do \
		$(CLANG) $(CHECK64_FLAGS) "$$f" || exit 1; \
	done
	@echo "check64: no 64-bit portability warnings"

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
	$(AS) -f $(ASM_FORMAT) -g -F dwarf -I $(dir $<) -I src/arch/x86/asm/ $< -o $@

# isr.asm (both versions) %includes the generated stub table
$(BUILD_DIR)/arch/x86/asm/isr.o $(BUILD_DIR)/arch/x86/asm64/isr.o: $(ISR_GEN_INC)

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

# the version header changes only with the commit, so a build of the same
# commit does not compile the kernel again.
VERSION_H := $(BUILD_DIR)/generated/version.h
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DATE   := $(shell git log -1 --format=%cs 2>/dev/null)
GIT_DIRTY  := $(shell git diff --quiet HEAD 2>/dev/null || echo +)

$(VERSION_H): FORCE
	@mkdir -p $(dir $@)
	@printf '#pragma once\n#define GINNOS_RELEASE "%s"\n#define GINNOS_VERSION "%s"\n' \
		'$(GINNOS_RELEASE)' '$(GIT_COMMIT)$(GIT_DIRTY) $(GIT_DATE)' > $@.tmp; \
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(BUILD_DIR)/kernel/syscall/sys_uname.o $(TEST_BUILD_DIR)/obj/kernel/syscall/sys_uname.o: $(VERSION_H)

# kernel objects rebuild when the kernel C flags change.
$(KERNEL_FLAGS_FILE): FORCE
	$(call update_if_changed,$@,$(KERNEL_CFLAGS))

$(KERNEL_C_OBJS) $(TEST_KERNEL_C_OBJS): $(KERNEL_FLAGS_FILE)

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

$(BUILD_DIR)/libc/arch/%.o: src/libc/arch/$(ARCH)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

$(LIBC_A): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(CROSS)ar rcs $@ $(LIBC_OBJS)

# user program build rules
$(USER_CRT0): $(USER_CRT0_SRC)
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

$(BUILD_DIR)/user/lib/crt%.o: src/user/lib/$(ARCH)/crt%.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) $< -o $@

# the libc headers, start files and libc.a where the ginnOS toolchain
# looks for them; build the toolchain with tools/toolchain/build.sh
sysroot: $(USER_CRT0) $(USER_CRTI) $(USER_CRTN) $(LIBC_A)
	@rm -rf $(SYSROOT)/usr/include
	@mkdir -p $(SYSROOT)/usr/include $(SYSROOT)/usr/lib
	cp -R src/libc/include/. $(SYSROOT)/usr/include/
	cp $(USER_CRT0) $(USER_CRTI) $(USER_CRTN) $(LIBC_A) $(SYSROOT)/usr/lib/
	@# the math functions are in libc.a: -lm finds an empty library
	@rm -f $(SYSROOT)/usr/lib/libm.a
	$(CROSS)ar rcs $(SYSROOT)/usr/lib/libm.a

# user libraries (src/lib/<name>/, see src/lib/lib.mk), built before the
# programs that use them
USER_LIB_DIRS := $(sort $(patsubst %/Makefile,%,$(wildcard src/lib/*/Makefile)))

user-libs: $(LIBC_A)
	@for d in $(USER_LIB_DIRS); do \
		$(MAKE) --no-print-directory -C $$d $(USER_MAKE_VARS) || exit 1; \
	done

# delegate to per-program Makefiles
user-programs: $(USER_CRT0) $(LIBC_A) user-libs
	@for d in $(USER_PROG_DIRS); do \
		$(MAKE) --no-print-directory -C $$d $(USER_MAKE_VARS) || exit 1; \
	done

# library tests on the host, with the host compiler (src/lib/host-test.mk).
# GOLDEN=update writes the golden images again.
HOST_TEST_DIRS := $(sort $(patsubst %/Makefile,%,$(wildcard src/lib/*/test/Makefile)))

host-test:
	@for d in $(HOST_TEST_DIRS); do \
		$(MAKE) --no-print-directory -C $$d GOLDEN=$(GOLDEN) \
			$(if $(SANITIZE),SANITIZE=$(SANITIZE)) || exit 1; \
	done

# root filesystem and disk image
# stage2 loads /boot/kernel.bin from the root filesystem (i686 only)
ifeq ($(ARCH),i686)
ROOTFS_KERNEL := $(KERNEL_BIN)
endif

# both packs come from one run of build.py
$(ICONS_OUT)/tela.pack: $(ICONS_OUT)/lucide.pack
$(ICONS_OUT)/lucide.pack: tools/icons/build.py tools/icons/fetch.sh tools/icons/lucide.list \
                          tools/icons/tela.list
	./tools/icons/fetch.sh $(ICONS_CACHE)
	$(PYTHON) -I tools/icons/build.py $(ICONS_CACHE)/src $(ICONS_OUT)

icons: $(ICONS_PACKS)

$(WALLPAPER_OUT): $(wildcard $(WALLPAPER)) tools/wallpaper/make.py
	$(PYTHON) tools/wallpaper/make.py "$(WALLPAPER)" $@ $(RESOLUTION)

rootfs-image: $(ROOTFS_KERNEL) user-programs $(if $(filter 1,$(ICONS)),$(ICONS_PACKS)) \
              $(if $(WALLPAPER),$(WALLPAPER_OUT))
	rm -rf $(ROOTFS_STAGING)
	@mkdir -p $(ROOTFS_STAGING) $(dir $(ROOTFS_IMAGE))
	cp -R $(EXT2_SOURCE_DIR)/. $(ROOTFS_STAGING)/
	@mkdir -p $(ROOTFS_STAGING)/boot $(ROOTFS_STAGING)/bin
	$(if $(ROOTFS_KERNEL),cp $(ROOTFS_KERNEL) $(ROOTFS_STAGING)/boot/kernel.bin)
	@for prog in $(USER_PROGRAMS); do \
		cp $$prog $(ROOTFS_STAGING)/bin/$$(basename $$prog); \
	done
	$(PYTHON) tools/sounds/make_sounds.py $(ROOTFS_STAGING)/usr/share/sounds
	$(if $(filter 1,$(FREEDOOM)),./$(FREEDOOM_FETCH) $(FREEDOOM_CACHE) $(ROOTFS_STAGING)/usr/share/doom)
	$(if $(filter 1,$(FONTS)),./$(FONTS_FETCH) $(FONTS_CACHE) $(ROOTFS_STAGING)/usr/share/fonts)
	$(if $(filter 1,$(ICONS)),mkdir -p $(ROOTFS_STAGING)/usr/share/icons && \
		cp $(ICONS_PACKS) $(ICONS_OUT)/LICENSE-lucide.txt $(ICONS_OUT)/COPYING-tela.txt \
		$(ROOTFS_STAGING)/usr/share/icons/)
	@if [ -n "$(WALLPAPER)" ] && [ -f $(WALLPAPER_OUT) ]; then \
		mkdir -p $(ROOTFS_STAGING)/usr/share/wallpapers; \
		cp $(WALLPAPER_OUT) $(ROOTFS_STAGING)/usr/share/wallpapers/default.png; \
	fi
	$(if $(filter 1,$(DOOM)),cp src/user/doom/doom.app $(ROOTFS_STAGING)/usr/share/applications/)
	@if [ "$(DOOM)" = 1 ] && [ -d "$(DOOM_WAD_DIR)" ]; then \
		for wad in "$(DOOM_WAD_DIR)"/*.[wW][aA][dD]; do \
			[ -f "$$wad" ] || continue; \
			mkdir -p $(ROOTFS_STAGING)/usr/share/doom; \
			name=$$(basename "$$wad" | tr 'A-Z' 'a-z'); \
			echo "doom: $$wad -> /usr/share/doom/$$name"; \
			cp "$$wad" "$(ROOTFS_STAGING)/usr/share/doom/$$name"; \
		done; \
	fi
	$(PYTHON) $(EXT2_IMAGE_TOOL) \
		--source "$(ROOTFS_STAGING)" \
		--output "$(ROOTFS_IMAGE)" \
		--min-size-mb $(EXT2_MIN_SIZE_MB) \
		$(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)

ifeq ($(BOOTLOADER),limine)
$(DISK_IMAGE): $(KERNEL_ELF) rootfs-image
	$(call limine_disk,$@,$(KERNEL_ELF),$(ROOTFS_IMAGE),$(BUILD_DIR)/boot/limine)
else
$(DISK_IMAGE): $(STAGE1_BIN) $(STAGE2_PAD) rootfs-image
	$(call assemble_disk,$@,$(ROOTFS_IMAGE))
endif

# the sound card for make run and run-uefi: an Intel HDA controller with a
# line out, played through the host's sound system (coreaudio on macOS,
# pa on Linux). AUDIO=ac97 uses an AC'97 card instead, AUDIO=0 none, and
# QEMU_AUDIODEV=<backend> picks another backend (alsa, sdl, wav,path=x.wav).
AUDIO ?= hda
ifeq ($(shell uname -s),Darwin)
QEMU_AUDIODEV ?= coreaudio
else
QEMU_AUDIODEV ?= pa
endif
ifeq ($(AUDIO),hda)
QEMU_AUDIO_FLAGS := -audiodev $(QEMU_AUDIODEV),id=snd0 -device intel-hda -device hda-output,audiodev=snd0
else ifeq ($(AUDIO),ac97)
QEMU_AUDIO_FLAGS := -audiodev $(QEMU_AUDIODEV),id=snd0 -device AC97,audiodev=snd0
else
QEMU_AUDIO_FLAGS :=
endif

run: check-tools $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) $(QEMU_AUDIO_FLAGS) -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

# boot the disk image with UEFI firmware (limine only: stage1 is BIOS-only).
# the i686 kernel also runs on an x86_64 CPU, so this uses the x86_64
# firmware for both.
run-uefi: check-tools $(DISK_IMAGE)
	@test "$(BOOTLOADER)" = limine || { echo "make run-uefi needs BOOTLOADER=limine"; exit 1; }
	@test -n "$(OVMF_CODE)" || { echo "no OVMF firmware found: set OVMF_CODE=<path to OVMF_CODE.fd>"; exit 1; }
	$(QEMU_UEFI) $(QEMU_FLAGS) $(QEMU_AUDIO_FLAGS) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

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

test-image: $(if $(filter limine,$(BOOTLOADER)),$(TEST_KERNEL_ELF),$(STAGE1_BIN) $(STAGE2_PAD)) \
            $(if $(ROOTFS_KERNEL),$(TEST_KERNEL_BIN)) rootfs-image
	rm -rf $(TEST_ROOTFS_DIR)
	@mkdir -p $(TEST_ROOTFS_DIR)
	cp -R $(ROOTFS_STAGING)/. $(TEST_ROOTFS_DIR)/
	$(if $(ROOTFS_KERNEL),cp $(TEST_KERNEL_BIN) $(TEST_ROOTFS_DIR)/boot/kernel.bin)
	$(PYTHON) $(EXT2_IMAGE_TOOL) \
		--source "$(TEST_ROOTFS_DIR)" \
		--output "$(TEST_ROOTFS_IMAGE)" \
		--min-size-mb $(EXT2_MIN_SIZE_MB) \
		$(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)
ifeq ($(BOOTLOADER),limine)
	$(call limine_disk,$(TEST_DISK_IMAGE),$(TEST_KERNEL_ELF),$(TEST_ROOTFS_IMAGE),$(TEST_BUILD_DIR)/limine)
else
	$(call assemble_disk,$(TEST_DISK_IMAGE),$(TEST_ROOTFS_IMAGE))
endif

# clean keeps the fetched limine and freedoom releases (build/limine,
# build/freedoom); distclean removes them
clean:
	@if [ -d $(BUILD_DIR) ]; then \
		find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name limine ! -name freedoom ! -name fonts ! -name icons -exec rm -rf {} +; \
	fi
	rm -rf $(EXT2_SOURCE_DIR)/boot $(EXT2_SOURCE_DIR)/bin
	rm -f $(ISR_GEN_C) $(ISR_GEN_INC)
	rm -f compile_commands.json

distclean: clean
	rm -rf $(BUILD_DIR)

# always-run prerequisite for the kernel object-list files.
FORCE:

# automatic header dependencies
-include $(DEP_FILES)
