# the build configuration: the architecture, the compilers and their flags,
# the build options (make VAR=value), and helpers the other parts use.
# this part has no rules.

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
# objects, because they depend on $(KERNEL_FLAGS_FILE) (mk/kernel.mk).
ifeq ($(BUILD_MODE),debug)
  KERNEL_CFLAGS := $(BASE_CFLAGS) $(BASE_WARNINGS) -Og -g -DDEBUG \
                   -fno-pie -fno-jump-tables $(KERNEL_FRAME_FLAGS) $(KERNEL_ARCH_CFLAGS)
  DRIVER_CFLAGS := $(KERNEL_CFLAGS)
endif

# linker flags
LDFLAGS        := -T $(LINKER_DIR)/kernel.ld -nostdlib -z max-page-size=0x1000
STAGE2_LDFLAGS := -T $(LINKER_DIR)/stage2.ld -nostdlib --gc-sections

# the build outputs

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
LIMINE_MENU_TOOL := tools/limine/boot_menu.py
# the size of the QEMU window (and of the wallpaper), for example
# RESOLUTION=1280x800. the virtio-gpu driver takes it at start.
RESOLUTION       ?= 1440x900
# the screen size the first boot entry asks for. empty: limine picks it
# (the monitor's own size when the firmware tells it, else the firmware's
# mode). set it for QEMU_VGA=std, which has no driver to change the mode.
BOOT_RESOLUTION  ?=
# the sizes in the boot menu's "Screen sizes" folder, for a computer whose
# firmware does not tell the monitor's size
BOOT_SIZES       ?= 3840x2160 2560x1600 2560x1440 1920x1200 1920x1080 1680x1050 1600x900 \
                    1440x900 1366x768 1280x800 1280x720 1024x768
# seconds the boot menu waits (a key stops the count)
BOOT_TIMEOUT     ?= 3
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

# write $(2) into file $(1), touching it only if the content changed.
define update_if_changed
	@mkdir -p $(dir $(1))
	@echo '$(2)' > $(1).tmp; \
	if cmp -s $(1).tmp $(1); then rm -f $(1).tmp; else mv $(1).tmp $(1); fi
endef
