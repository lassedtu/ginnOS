# Makefile for ze (zen editor) — ginnOS userspace build
#
# Builds ze from the zen-editor source tree (included as a git submodule).
# After cloning ginnOS, run: git submodule update --init
#
# The submodule lives at src/user/ze/zen-editor/

PROG := ze

ROOT     := ../../..
BUILD    := $(ROOT)/build/user
LIBC_INC := $(ROOT)/src/libc/include

ZE_SRC   := zen-editor

CROSS   ?= i686-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld

CFLAGS  := -std=c99 -ffreestanding -O2 -Wall -Wextra -m32 \
           -fno-pie -fno-stack-protector -nostdlib \
           -I$(ZE_SRC)/include -I$(LIBC_INC) -MMD -MP

LDFLAGS := -T $(ROOT)/linker/user.ld -nostdlib

CRT0    := $(BUILD)/lib/crt0.o
LIBC    := $(ROOT)/build/libc/libc.a

OBJDIR  := $(BUILD)/$(PROG)
TARGET  := $(BUILD)/bin/$(PROG)

# Core sources (from zen-editor/src/)
CORE_SRCS := buffer.c command.c cursor.c editor.c keymap.c main.c renderer.c undo.c

# Platform sources (from zen-editor/platforms/ginnos/)
PLAT_SRCS := terminal.c fs.c

# Object files
CORE_OBJS := $(patsubst %.c,$(OBJDIR)/core/%.o,$(CORE_SRCS))
PLAT_OBJS := $(patsubst %.c,$(OBJDIR)/plat/%.o,$(PLAT_SRCS))
OBJS      := $(CORE_OBJS) $(PLAT_OBJS)

DEP_FILES := $(OBJS:.o=.d)

.PHONY: all
all: $(TARGET)

# Check that the submodule is initialized
ifeq ($(wildcard $(ZE_SRC)/src/main.c),)
$(error zen-editor submodule not found. Run: git submodule update --init)
endif

# Core: compile from zen-editor/src/
$(OBJDIR)/core/%.o: $(ZE_SRC)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Platform: compile from zen-editor/platforms/ginnos/
$(OBJDIR)/plat/%.o: $(ZE_SRC)/platforms/ginnos/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0) $(LIBC)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIBC)

-include $(DEP_FILES)
