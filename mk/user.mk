# our own user libraries and programs, and their tests on the host

.PHONY: user-libs user-programs host-test

# user programs: automatic discovery via per-program Makefiles.
# each program lives in src/user/<name>/ with its own Makefile.
# src/user/lib/ is excluded (it contains crt0, not a program).
# src/user/doom is built only with DOOM=1.
USER_PROG_DIRS := $(sort $(filter-out src/user/lib $(if $(filter 1,$(DOOM)),,src/user/doom),\
                  $(patsubst %/Makefile,%,$(wildcard src/user/*/Makefile))))
USER_PROG_NAMES := $(notdir $(USER_PROG_DIRS))
USER_PROGRAMS := $(addprefix $(BUILD_DIR)/user/bin/,$(USER_PROG_NAMES))

# what the per-program Makefiles (src/user/program.mk) need to know
USER_MAKE_VARS := CROSS=$(CROSS) BUILD_ROOT=$(abspath $(BUILD_DIR)) \
                  ARCH_CFLAGS=$(ARCH_CFLAGS) USER_LD_SCRIPT=$(abspath $(LINKER_DIR)/user.ld) \
                  TOOLCHAIN_FLAGS="$(TOOLCHAIN_FLAGS)"

# user libraries (src/lib/<name>/, see src/lib/lib.mk), built before the
# programs that use them
USER_LIB_DIRS := $(sort $(patsubst %/Makefile,%,$(wildcard src/lib/*/Makefile)))

user-libs: $(LIBC_A)
	@for d in $(USER_LIB_DIRS); do \
		$(MAKE) --no-print-directory -C $$d $(USER_MAKE_VARS) || exit 1; \
	done

# delegate to per-program Makefiles. a program can use a library port
# (fetch uses mbedtls): those ports come first.
USER_PORT_LIBS := $(filter %/mbedtls.done,$(PORT_STAMPS))
user-programs: $(USER_CRT0) $(LIBC_A) user-libs $(USER_PORT_LIBS)
	@for d in $(USER_PROG_DIRS); do \
		$(MAKE) --no-print-directory -C $$d $(USER_MAKE_VARS) || exit 1; \
	done

# library and program tests on the host, with the host compiler
# (src/lib/host-test.mk).
# GOLDEN=update writes the golden images again.
HOST_TEST_DIRS := $(sort $(patsubst %/Makefile,%,$(wildcard src/lib/*/test/Makefile src/libc/test/*/Makefile \
                                                             src/user/*/test/Makefile)))

host-test:
	@for d in $(HOST_TEST_DIRS); do \
		$(MAKE) --no-print-directory -C $$d GOLDEN=$(GOLDEN) \
			$(if $(SANITIZE),SANITIZE=$(SANITIZE)) || exit 1; \
	done
