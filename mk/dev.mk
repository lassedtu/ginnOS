# tools for development: the tool check, lint, format, check64 and
# compile_commands

.PHONY: check-tools lint check64 format compile_commands

# all source files for lint/format targets
ALL_C_SRCS := $(COMMON_C_SRCS) $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS) \
              $(STAGE2_C_SRCS) $(LIBC_C_SRCS) $(TEST_C_SRCS)
ALL_H_SRCS := $(shell find src -name '*.h' -not -path '*/zen-editor/*' 2>/dev/null | sort)

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "missing tool: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "missing tool: $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "missing tool: $(OBJCOPY)"; exit 1; }
	@command -v $(NM) >/dev/null 2>&1 || { echo "missing tool: $(NM)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "missing tool: $(AS)"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "missing tool: $(QEMU)"; exit 1; }
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "missing tool: $(PYTHON)"; exit 1; }
ifeq ($(BOOTLOADER),limine)
	@command -v mformat >/dev/null 2>&1 || { echo "missing tool: mformat (install mtools)"; exit 1; }
	@command -v git >/dev/null 2>&1 || { echo "missing tool: git (to fetch limine)"; exit 1; }
endif

# static analysis via clang-tidy (uses .clang-tidy config)
lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "install clang-tidy to use make lint"; exit 1; }
	clang-tidy $(filter %.c,$(ALL_C_SRCS)) $(ALL_H_SRCS) -- $(KERNEL_CFLAGS)

# x86_64 readiness check: a syntax-only x86_64 compile (host clang) of the
# kernel, drivers, fs, common code and tests. any pointer/integer cast,
# 64-to-32-bit narrowing or printf format mismatch is an error, so code that
# only works because pointers are 32 bits is caught before the x86_64 port.
CLANG ?= clang
CHECK64_FLAGS := --target=x86_64-elf -ffreestanding -fsyntax-only -std=gnu11 -I src \
                 -DKERNEL_TEST -Wno-everything -Werror -Wpointer-to-int-cast \
                 -Wint-to-pointer-cast -Wvoid-pointer-to-int-cast -Wshorten-64-to-32 \
                 -Wformat

check64:
	@command -v $(CLANG) >/dev/null 2>&1 || { echo "install clang to use make check64"; exit 1; }
	@for f in $(KERNEL_C_SRCS) $(COMMON_C_SRCS) $(TEST_C_SRCS); do \
		$(CLANG) $(CHECK64_FLAGS) "$$f" || exit 1; \
	done
	@echo "check64: no 64-bit portability warnings"

# auto-format via clang-format (uses .clang-format config)
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "install clang-format to use make format"; exit 1; }
	clang-format -i $(filter %.c,$(ALL_C_SRCS)) $(ALL_H_SRCS)

# generate compile_commands.json for clangd/lsp support.
# uses a simple python script to build from the Makefile's dry-run output.
compile_commands:
	@command -v bear >/dev/null 2>&1 && { \
		$(MAKE) clean; \
		bear -- $(MAKE) all; \
	} || { \
		echo '[]' > compile_commands.json; \
		$(MAKE) clean; \
		$(MAKE) all CC_JSON=1 2>&1 | grep '$(CC)' | \
		$(PYTHON) -c "\
import sys, json, os; \
entries = []; \
for line in sys.stdin: \
    parts = line.strip().split(); \
    if not parts: continue; \
    src = next((p for p in parts if p.endswith('.c')), None); \
    if src: entries.append({'directory': os.getcwd(), 'command': line.strip(), 'file': src}); \
json.dump(entries, open('compile_commands.json','w'), indent=2)"; \
		echo "generated compile_commands.json"; \
	}
