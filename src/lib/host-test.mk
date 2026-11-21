# host-test.mk - build and run a library's tests on the host (make host-test)
#
# the tests of src/lib/<name>/ live in its test/ directory, with a Makefile
# that sets these and includes this file:
#   TEST     - the test program's name
#   SRCS     - the test sources (in test/)
#   LIB_SRCS - the library sources to test, relative to test/
# optional:
#   LIBS         - other libraries the test needs (their sources are built in)
#   EXTRA_CFLAGS - more compiler flags
#
# the host compiler builds them with the undefined behaviour sanitizer.
# SANITIZE=address,undefined adds the address sanitizer where it works (on
# some macOS versions it stops at start-up). golden images are in test/golden; run with GOLDEN=update to
# write them again (then look at them before committing).

ROOT      := ../../../..
HOST_CC   ?= cc
SANITIZE  ?= undefined
OUT       := $(ROOT)/build/host-test/$(TEST)
HOSTTEST  := $(ROOT)/src/lib/hosttest

# the sources of the other libraries, from their Makefiles' SRCS
lib_srcs = $(addprefix $(ROOT)/src/lib/$(1)/,$(shell $(MAKE) -s --no-print-directory \
             -C $(ROOT)/src/lib/$(1) print-srcs 2>/dev/null))

CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=$(SANITIZE) \
          -fno-sanitize-recover=undefined -I../include -I$(HOSTTEST) \
          $(foreach lib,$(LIBS),-I$(ROOT)/src/lib/$(lib)/include) $(EXTRA_CFLAGS)

ALL_SRCS := $(SRCS) $(LIB_SRCS) $(HOSTTEST)/hosttest.c \
            $(foreach lib,$(LIBS),$(call lib_srcs,$(lib)))

.PHONY: run
run: $(OUT)/$(TEST)
	cd $(OUT) && GOLDEN_DIR=$(abspath golden) GOLDEN=$(GOLDEN) ./$(TEST)

$(OUT)/$(TEST): $(ALL_SRCS) $(ROOT)/src/lib/host-test.mk Makefile $(wildcard ../include/*.h ../*.h *.h $(HOSTTEST)/*.h)
	@mkdir -p $(OUT)
	$(HOST_CC) $(CFLAGS) $(ALL_SRCS) -lm -o $@
