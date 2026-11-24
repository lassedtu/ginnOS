# the old BIOS bootloader (stage1 and stage2), and how a disk image is laid
# out for it or for limine

STAGE1_SRC := src/bootloader/stage1/boot.asm

STAGE2_C_DIRS    := src/bootloader/stage2 src/drivers/disk src/fs/ext2
STAGE2_ENTRY_ASM := src/bootloader/stage2/main.asm

STAGE2_C_SRCS    := $(foreach d,$(STAGE2_C_DIRS),$(call find_c_sources,$(d)))
STAGE2_C_OBJS    := $(call src_c_to_stage2,$(STAGE2_C_SRCS))
STAGE2_ENTRY_OBJ := $(BUILD_DIR)/stage2/bootloader/stage2/entry.o

STAGE2_LINK_OBJS := $(STAGE2_ENTRY_OBJ) $(sort $(STAGE2_C_OBJS)) $(COMMON_OBJS)

DEP_FILES += $(STAGE2_C_OBJS:.o=.d)

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
	$(PYTHON) $(LIMINE_MENU_TOOL) $(LIMINE_CONF) $(4)/limine.conf --timeout $(BOOT_TIMEOUT) \
		$(if $(BOOT_RESOLUTION),--resolution $(BOOT_RESOLUTION)) --sizes "$(BOOT_SIZES)"
	$(PYTHON) $(LIMINE_DISK_TOOL) --output $(1) --ext2 $(3) --limine-dir $(LIMINE_DIR) $(if $(filter 1,$(GPT)),--gpt) \
		--file $(4)/limine.conf:/boot/limine/limine.conf \
		--file $(LIMINE_DIR)/limine-bios.sys:/boot/limine/limine-bios.sys \
		--file $(LIMINE_DIR)/BOOTX64.EFI:/EFI/BOOT/BOOTX64.EFI \
		--file $(LIMINE_DIR)/BOOTIA32.EFI:/EFI/BOOT/BOOTIA32.EFI \
		--file $(4)/kernel.elf:/boot/kernel.elf \
		--file $(3):/boot/rootfs.ext2
endef

# stage 1 bootloader flat binary, no C
$(STAGE1_BIN): $(STAGE1_SRC)
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_BIN_FMT) $< -o $@
	@test $$(wc -c < $@) -eq 512 || { echo "stage1 must be exactly 512 bytes"; exit 1; }

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
