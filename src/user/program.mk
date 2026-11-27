# program.mk - shared build rules for ginnOS userspace programs
#
# Include this from each per-program Makefile after setting:
#   PROG  - binary name (e.g. "cat")
#   SRCS  - list of source files (e.g. cat.c)
#
# Optional:
#   EXTRA_CFLAGS - additional compiler flags
#   LIBS         - user libraries to link, by name (src/lib/<name>, e.g. gfx);
#                  a library that uses another comes before it
#   EXTRA_LIBS   - other archives to link, as paths (a port's library in
#                  the sysroot); they come before libc

ROOT     := ../../..
# the top Makefile passes these for the architecture it builds
BUILD_ROOT     ?= $(ROOT)/build
ARCH_CFLAGS    ?= -m32
USER_LD_SCRIPT ?= $(ROOT)/linker/user.ld
TOOLCHAIN_FLAGS ?=

BUILD    := $(BUILD_ROOT)/user
LIBC_INC := $(ROOT)/src/libc/include

CROSS   ?= i686-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
AS      := nasm

CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra $(ARCH_CFLAGS) $(TOOLCHAIN_FLAGS) \
           -fno-pie -fno-stack-protector -nostdlib \
           -I$(LIBC_INC) -I. $(foreach lib,$(LIBS),-I$(ROOT)/src/lib/$(lib)/include) \
           -MMD -MP $(EXTRA_CFLAGS)
LDFLAGS := -T $(USER_LD_SCRIPT) -nostdlib

CRT0    := $(BUILD)/lib/crt0.o
LIBC    := $(BUILD_ROOT)/libc/libc.a
LIB_ARCHIVES := $(foreach lib,$(LIBS),$(BUILD_ROOT)/lib/lib$(lib).a)
# gcc's helper functions (64-bit division on i686, for example)
LIBGCC  := $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name)

OBJDIR  := $(BUILD)/$(PROG)
OBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
TARGET  := $(BUILD)/bin/$(PROG)

DEP_FILES := $(OBJS:.o=.d)

.PHONY: all
all: $(TARGET)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0) $(LIB_ARCHIVES) $(EXTRA_LIBS) $(LIBC)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIB_ARCHIVES) $(EXTRA_LIBS) $(LIBC) $(LIBGCC)

-include $(DEP_FILES)
