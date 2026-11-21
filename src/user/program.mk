# program.mk - shared build rules for ginnOS userspace programs
#
# Include this from each per-program Makefile after setting:
#   PROG  - binary name (e.g. "cat")
#   SRCS  - list of source files (e.g. cat.c)
#
# Optional:
#   EXTRA_CFLAGS - additional compiler flags

ROOT     := ../../..
# the top Makefile passes these for the architecture it builds
BUILD_ROOT     ?= $(ROOT)/build
ARCH_CFLAGS    ?= -m32
USER_LD_SCRIPT ?= $(ROOT)/linker/user.ld

BUILD    := $(BUILD_ROOT)/user
LIBC_INC := $(ROOT)/src/libc/include

CROSS   ?= i686-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
AS      := nasm

CFLAGS  := -std=gnu11 -ffreestanding -O2 -Wall -Wextra $(ARCH_CFLAGS) \
           -fno-pie -fno-stack-protector -nostdlib \
           -I$(LIBC_INC) -I. -MMD -MP $(EXTRA_CFLAGS)
LDFLAGS := -T $(USER_LD_SCRIPT) -nostdlib

CRT0    := $(BUILD)/lib/crt0.o
LIBC    := $(BUILD_ROOT)/libc/libc.a

OBJDIR  := $(BUILD)/$(PROG)
OBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
TARGET  := $(BUILD)/bin/$(PROG)

DEP_FILES := $(OBJS:.o=.d)

.PHONY: all
all: $(TARGET)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0) $(LIBC)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIBC)

-include $(DEP_FILES)
