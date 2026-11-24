# the C library, the start files, and the sysroot of the ginnOS toolchain

.PHONY: sysroot

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

DEP_FILES += $(LIBC_C_OBJS:.o=.d)

# libc build rules
# the regex files are musl's as musl has them (src/libc/src/regex/README.md);
# they compare signed with unsigned and fall through cases on purpose
$(BUILD_DIR)/libc/regex/%.o: LIBC_CFLAGS += -Wno-sign-compare -Wno-implicit-fallthrough
# the same for musl's number conversions (src/libc/src/float/README.md),
# which also leave out parentheses that only show the order of operations,
# and fill a 16-byte table with the 16 digits of a string
$(BUILD_DIR)/libc/float/%.o: LIBC_CFLAGS += -Wno-sign-compare -Wno-parentheses \
                                          -Wno-unterminated-string-initialization

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
# looks for them; build the toolchain with tools/toolchain/build.sh. the
# stamp file is new when the sysroot changed, for the ports (mk/ports.mk).
SYSROOT_STAMP := $(SYSROOT)/.done
LIBC_HEADERS  := $(shell find src/libc/include -type f 2>/dev/null | sort)

sysroot: $(SYSROOT_STAMP)

$(SYSROOT_STAMP): $(USER_CRT0) $(USER_CRTI) $(USER_CRTN) $(LIBC_A) $(LIBC_HEADERS)
	@rm -rf $(SYSROOT)/usr/include
	@mkdir -p $(SYSROOT)/usr/include $(SYSROOT)/usr/lib
	cp -R src/libc/include/. $(SYSROOT)/usr/include/
	cp $(USER_CRT0) $(USER_CRTI) $(USER_CRTN) $(LIBC_A) $(SYSROOT)/usr/lib/
	@# the math functions are in libc.a: -lm finds an empty library
	@rm -f $(SYSROOT)/usr/lib/libm.a
	$(CROSS)ar rcs $(SYSROOT)/usr/lib/libm.a
	@touch $@
