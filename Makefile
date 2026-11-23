# the ginnOS build. the parts are in mk/:
#   config.mk  the architecture, the compilers, the build options
#   kernel.mk  the kernel and the common code
#   boot.mk    the old BIOS bootloader, and the disk layouts
#   libc.mk    the C library and the sysroot
#   user.mk    our own libraries and programs
#   image.mk   the root filesystem and the disk image
#   test.mk    the in-kernel tests
#   run.mk     QEMU and gdb
#   dev.mk     lint, format, check64, compile_commands
#
# make            the disk image (build/ginnos.img; ARCH=x86_64 for 64-bit)
# make run        the disk image in QEMU
# make test       the in-kernel tests
# make host-test  the library and program tests on the host

.DEFAULT_GOAL := all

include mk/config.mk
include mk/kernel.mk
include mk/boot.mk
include mk/libc.mk
include mk/user.mk
include mk/image.mk
include mk/test.mk
include mk/run.mk
include mk/dev.mk

.PHONY: all clean distclean FORCE

all: check-tools $(DISK_IMAGE)

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
