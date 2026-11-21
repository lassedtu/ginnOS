# lib.mk - shared build rules for ginnOS user libraries
#
# a library lives in src/lib/<name>/: its public headers in include/, its
# sources next to its Makefile, which sets these and includes this file:
#   LIB  - the name; the archive is lib<name>.a
#   SRCS - the source files
# optional:
#   LIBS         - other libraries whose headers this one uses
#   EXTRA_CFLAGS - more compiler flags
#
# a program uses a library with LIBS := <name> in its Makefile
# (src/user/program.mk).

ROOT := ../../..
# the top Makefile passes these for the architecture it builds
BUILD_ROOT      ?= $(ROOT)/build
ARCH_CFLAGS     ?= -m32
TOOLCHAIN_FLAGS ?=

CROSS ?= i686-elf-
CC    := $(CROSS)gcc
AR    := $(CROSS)ar

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra $(ARCH_CFLAGS) $(TOOLCHAIN_FLAGS) \
          -fno-pie -fno-stack-protector -nostdlib \
          -I$(ROOT)/src/libc/include -Iinclude \
          $(foreach lib,$(LIBS),-I$(ROOT)/src/lib/$(lib)/include) \
          -MMD -MP $(EXTRA_CFLAGS)

OBJDIR := $(BUILD_ROOT)/lib/$(LIB)
OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
TARGET := $(BUILD_ROOT)/lib/lib$(LIB).a

.PHONY: all
all: $(TARGET)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

-include $(OBJS:.o=.d)

# the source list, for the host tests of other libraries (host-test.mk)
.PHONY: print-srcs
print-srcs:
	@echo $(SRCS)
