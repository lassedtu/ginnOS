# the kernel, and the common code it shares with stage2

KERNEL_C_DIRS   := src/kernel $(ARCH_DIRS) src/drivers src/fs
COMMON_C_DIR    := src/common

# isr code generation
ISR_GEN_SCRIPT := tools/generate_isrs.sh
ISR_GEN_C      := src/arch/x86/cpu/isr_gen.c
ISR_GEN_INC    := src/arch/x86/asm/isr_gen.inc

# automatic source discovery
COMMON_C_SRCS := $(call find_c_sources,$(COMMON_C_DIR))
COMMON_OBJS   := $(call src_c_to_obj,$(COMMON_C_SRCS))

# the kernel sources, with the generated isr table
KERNEL_C_SRCS := $(sort $(foreach d,$(KERNEL_C_DIRS),$(call find_c_sources,$(d))) \
                         $(ISR_GEN_C))
# the 64-bit assembly is in src/arch/x86/asm64; the rest is 32-bit
ifeq ($(ARCH),x86_64)
KERNEL_ASM_SRCS := $(call find_asm_sources,src/arch/x86/asm64)
else
KERNEL_ASM_SRCS := $(filter-out src/arch/x86/asm64/%,$(call find_asm_sources,src/kernel src/arch/x86))
endif

KERNEL_C_OBJS   := $(call src_c_to_obj,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(call src_asm_to_obj,$(KERNEL_ASM_SRCS))
ifeq ($(ARCH),x86_64)
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/arch/x86/asm64/entry.o
else
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/kernel/main.o
endif

# link order
KERNEL_LINK_OBJS := $(KERNEL_ENTRY_OBJ) \
                    $(filter-out $(KERNEL_ENTRY_OBJ),$(sort $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS))) \
                    $(COMMON_OBJS)

# dependency files (C compilations only)
DEP_FILES += $(KERNEL_C_OBJS:.o=.d) $(COMMON_OBJS:.o=.d)

# a file recording the kernel's link object list (mk/test.mk has the test
# kernel's). it is rewritten only when the list changes, so adding or deleting
# a source file relinks the kernel (a link rule otherwise only notices objects
# that are newer than the binary, not ones that disappeared).
KERNEL_OBJS_LIST      := $(BUILD_DIR)/kernel.objs

# two-pass kernel link with an embedded symbol table (src/kernel/debug/ksyms.h).
# pass 1 links an empty table to learn the function addresses, pass 2 links the
# real table. .ksyms sits after .text, so function addresses are the same in
# both passes; the final --check fails the build if that ever stops holding.
#   $(1) output elf, $(2) link objects, $(3) scratch dir, $(4) C flags
KSYMS_TOOL := tools/gen_ksyms.py

define link_with_ksyms
	@mkdir -p $(3)
	$(PYTHON) $(KSYMS_TOOL) -o $(3)/ksyms_empty.c < /dev/null
	$(CC) $(4) -c $(3)/ksyms_empty.c -o $(3)/ksyms_empty.o
	$(LD) $(LDFLAGS) -o $(1).pass1 $(2) $(3)/ksyms_empty.o
	$(NM) -n $(1).pass1 | $(PYTHON) $(KSYMS_TOOL) -o $(3)/ksyms.c
	$(CC) $(4) -c $(3)/ksyms.c -o $(3)/ksyms.o
	$(LD) $(LDFLAGS) -o $(1) $(2) $(3)/ksyms.o
	$(NM) -n $(1) | $(PYTHON) $(KSYMS_TOOL) --check $(3)/ksyms.c
	@rm -f $(1).pass1
endef

# file recording the kernel C flags, rewritten only when they change, so
# kernel objects rebuild when the flags do (e.g. make debug after make all).
KERNEL_FLAGS_FILE := $(BUILD_DIR)/kernel.flags

# isr generation
$(ISR_GEN_C) $(ISR_GEN_INC): $(ISR_GEN_SCRIPT)
	@mkdir -p $(dir $(ISR_GEN_C)) $(dir $(ISR_GEN_INC))
	./$(ISR_GEN_SCRIPT) $(ISR_GEN_C) $(ISR_GEN_INC)

# pattern rules: shared library code (compiled once, linked into stage2 + kernel)
$(BUILD_DIR)/common/%.o: src/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

# pattern rules: kernel (C + nasm elf32)
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f $(ASM_FORMAT) -g -F dwarf -I $(dir $<) -I src/arch/x86/asm/ $< -o $@

# isr.asm (both versions) %includes the generated stub table
$(BUILD_DIR)/arch/x86/asm/isr.o $(BUILD_DIR)/arch/x86/asm64/isr.o: $(ISR_GEN_INC)

# the version header changes only with the commit, so a build of the same
# commit does not compile the kernel again.
VERSION_H := $(BUILD_DIR)/generated/version.h
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DATE   := $(shell git log -1 --format=%cs 2>/dev/null)
GIT_DIRTY  := $(shell git diff --quiet HEAD 2>/dev/null || echo +)

$(VERSION_H): FORCE
	@mkdir -p $(dir $@)
	@printf '#pragma once\n#define GINNOS_RELEASE "%s"\n#define GINNOS_VERSION "%s"\n' \
		'$(GINNOS_RELEASE)' '$(GIT_COMMIT)$(GIT_DIRTY) $(GIT_DATE)' > $@.tmp; \
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(BUILD_DIR)/kernel/syscall/sys_uname.o: $(VERSION_H)

# kernel objects rebuild when the kernel C flags change.
$(KERNEL_FLAGS_FILE): FORCE
	$(call update_if_changed,$@,$(KERNEL_CFLAGS))

$(KERNEL_C_OBJS): $(KERNEL_FLAGS_FILE)

# kernel link
$(KERNEL_OBJS_LIST): FORCE
	$(call update_if_changed,$@,$(KERNEL_LINK_OBJS))

$(KERNEL_ELF): $(KERNEL_LINK_OBJS) $(KERNEL_OBJS_LIST) $(LINKER_DIR)/kernel.ld $(ISR_GEN_C) $(ISR_GEN_INC) $(KSYMS_TOOL)
	$(call link_with_ksyms,$@,$(KERNEL_LINK_OBJS),$(BUILD_DIR)/ksyms,$(KERNEL_CFLAGS))

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
