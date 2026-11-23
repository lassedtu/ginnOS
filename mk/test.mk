# the in-kernel tests (make test)

.PHONY: test test-image

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

TEST_KERNEL_OBJS_LIST := $(TEST_BUILD_DIR)/kernel.objs

$(TEST_BUILD_DIR)/obj/kernel/syscall/sys_uname.o: $(VERSION_H)
$(TEST_KERNEL_C_OBJS): $(KERNEL_FLAGS_FILE)

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
