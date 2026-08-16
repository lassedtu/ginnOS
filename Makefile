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

# User programs
USER_CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
                -fno-pie -fno-stack-protector -nostdlib \
                -Isrc/libc/include -MMD -MP
USER_LDFLAGS := -T linker/user.ld -nostdlib
USER_CRT0    := $(BUILD_DIR)/user/lib/crt0.o

# Libc
LIBC_CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m32 \
                -fno-pie -fno-stack-protector -Isrc/libc/include -MMD -MP
LIBC_C_SRCS  := $(shell find src/libc/src -name '*.c' 2>/dev/null | sort)
LIBC_ASM_SRCS := $(shell find src/libc/src -name '*.asm' 2>/dev/null | sort)
LIBC_C_OBJS  := $(patsubst src/libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_C_SRCS))
LIBC_ASM_OBJS := $(patsubst src/libc/src/%.asm,$(BUILD_DIR)/libc/%.o,$(LIBC_ASM_SRCS))
LIBC_OBJS    := $(LIBC_C_OBJS) $(LIBC_ASM_OBJS)
LIBC_A       := $(BUILD_DIR)/libc/libc.a

# List of user programs (add new ones here)
USER_PROGRAMS := $(BUILD_DIR)/user/bin/hello \
                 $(BUILD_DIR)/user/bin/sbrk_test \
                 $(BUILD_DIR)/user/bin/waitpid_test \
                 $(BUILD_DIR)/user/bin/sh \
                 $(BUILD_DIR)/user/bin/echo \
                 $(BUILD_DIR)/user/bin/pwd \
                 $(BUILD_DIR)/user/bin/cat \
                 $(BUILD_DIR)/user/bin/ls \
                 $(BUILD_DIR)/user/bin/mkdir \
                 $(BUILD_DIR)/user/bin/touch \
                 $(BUILD_DIR)/user/bin/rm \
                 $(BUILD_DIR)/user/bin/rmdir \
                 $(BUILD_DIR)/user/bin/clear

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

# Libc build rules
$(BUILD_DIR)/libc/%.o: src/libc/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/%.o: src/libc/src/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(LIBC_A): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(CROSS)ar rcs $@ $(LIBC_OBJS)

# User program build rules
$(USER_CRT0): src/user/lib/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/user/bin/hello: src/user/hello/hello.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/hello/hello.c -o $(BUILD_DIR)/user/hello.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/hello.o $(LIBC_A)

$(BUILD_DIR)/user/bin/sbrk_test: src/user/sbrk_test/sbrk_test.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/sbrk_test/sbrk_test.c -o $(BUILD_DIR)/user/sbrk_test.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/sbrk_test.o $(LIBC_A)

$(BUILD_DIR)/user/bin/waitpid_test: src/user/waitpid_test/waitpid_test.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/waitpid_test/waitpid_test.c -o $(BUILD_DIR)/user/waitpid_test.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/waitpid_test.o $(LIBC_A)

SH_SRCS := $(wildcard src/user/sh/*.c)
SH_OBJS := $(patsubst src/user/sh/%.c,$(BUILD_DIR)/user/sh/%.o,$(SH_SRCS))

$(BUILD_DIR)/user/bin/sh: $(SH_SRCS) $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(BUILD_DIR)/user/sh $(dir $@)
	@for f in $(SH_SRCS); do \
		obj=$(BUILD_DIR)/user/sh/$$(basename $$f .c).o; \
		$(CC) $(USER_CFLAGS) -Isrc/user/sh -c $$f -o $$obj; \
	done
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(SH_OBJS) $(LIBC_A)

$(BUILD_DIR)/user/bin/echo: src/user/echo/echo.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/echo/echo.c -o $(BUILD_DIR)/user/echo.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/echo.o $(LIBC_A)

$(BUILD_DIR)/user/bin/pwd: src/user/pwd/pwd.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/pwd/pwd.c -o $(BUILD_DIR)/user/pwd.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/pwd.o $(LIBC_A)

$(BUILD_DIR)/user/bin/cat: src/user/cat/cat.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/cat/cat.c -o $(BUILD_DIR)/user/cat.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/cat.o $(LIBC_A)

$(BUILD_DIR)/user/bin/ls: src/user/ls/ls.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/ls/ls.c -o $(BUILD_DIR)/user/ls.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/ls.o $(LIBC_A)

$(BUILD_DIR)/user/bin/mkdir: src/user/mkdir_cmd/mkdir.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/mkdir_cmd/mkdir.c -o $(BUILD_DIR)/user/mkdir.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/mkdir.o $(LIBC_A)

$(BUILD_DIR)/user/bin/touch: src/user/touch/touch.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/touch/touch.c -o $(BUILD_DIR)/user/touch.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/touch.o $(LIBC_A)

$(BUILD_DIR)/user/bin/rm: src/user/rm/rm.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/rm/rm.c -o $(BUILD_DIR)/user/rm.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/rm.o $(LIBC_A)

$(BUILD_DIR)/user/bin/rmdir: src/user/rmdir_cmd/rmdir.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/rmdir_cmd/rmdir.c -o $(BUILD_DIR)/user/rmdir.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/rmdir.o $(LIBC_A)

$(BUILD_DIR)/user/bin/clear: src/user/clear/clear.c $(USER_CRT0) $(LIBC_A) linker/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c src/user/clear/clear.c -o $(BUILD_DIR)/user/clear.o
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(BUILD_DIR)/user/clear.o $(LIBC_A)

user-programs: $(USER_PROGRAMS)

# Root filesystem and disk image
rootfs-image: $(KERNEL_BIN) user-programs
	@mkdir -p $(EXT2_SOURCE_DIR)/boot $(EXT2_SOURCE_DIR)/bin $(dir $(ROOTFS_IMAGE))
	cp $(KERNEL_BIN) $(EXT2_SOURCE_DIR)/boot/kernel.bin
	cp $(BUILD_DIR)/user/bin/hello $(EXT2_SOURCE_DIR)/bin/hello
	cp $(BUILD_DIR)/user/bin/sbrk_test $(EXT2_SOURCE_DIR)/bin/sbrk_test
	cp $(BUILD_DIR)/user/bin/waitpid_test $(EXT2_SOURCE_DIR)/bin/waitpid_test
	cp $(BUILD_DIR)/user/bin/sh $(EXT2_SOURCE_DIR)/bin/sh
	cp $(BUILD_DIR)/user/bin/echo $(EXT2_SOURCE_DIR)/bin/echo
	cp $(BUILD_DIR)/user/bin/pwd $(EXT2_SOURCE_DIR)/bin/pwd
	cp $(BUILD_DIR)/user/bin/cat $(EXT2_SOURCE_DIR)/bin/cat
	cp $(BUILD_DIR)/user/bin/ls $(EXT2_SOURCE_DIR)/bin/ls
	cp $(BUILD_DIR)/user/bin/mkdir $(EXT2_SOURCE_DIR)/bin/mkdir
	cp $(BUILD_DIR)/user/bin/touch $(EXT2_SOURCE_DIR)/bin/touch
	cp $(BUILD_DIR)/user/bin/rm $(EXT2_SOURCE_DIR)/bin/rm
	cp $(BUILD_DIR)/user/bin/rmdir $(EXT2_SOURCE_DIR)/bin/rmdir
	cp $(BUILD_DIR)/user/bin/clear $(EXT2_SOURCE_DIR)/bin/clear
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
	rm -f $(EXT2_SOURCE_DIR)/bin/hello
	rm -f $(EXT2_SOURCE_DIR)/bin/sbrk_test
	rm -f $(EXT2_SOURCE_DIR)/bin/waitpid_test
	rm -f $(EXT2_SOURCE_DIR)/bin/sh
	rm -f $(EXT2_SOURCE_DIR)/bin/echo
	rm -f $(EXT2_SOURCE_DIR)/bin/pwd
	rm -f $(EXT2_SOURCE_DIR)/bin/cat
	rm -f $(EXT2_SOURCE_DIR)/bin/ls
	rm -f $(EXT2_SOURCE_DIR)/bin/mkdir
	rm -f $(EXT2_SOURCE_DIR)/bin/touch
	rm -f $(EXT2_SOURCE_DIR)/bin/rm
	rm -f $(EXT2_SOURCE_DIR)/bin/rmdir
	rm -f $(EXT2_SOURCE_DIR)/bin/clear
	rm -f $(ISR_GEN_C) $(ISR_GEN_INC)

# Automatic header dependencies
-include $(DEP_FILES)
