TEST     := bragi
SRCS     := bragi_test.c
LIB_SRCS := ../bragi_font.c ../bragi_cache.c ../bragi_text.c ../bragi_cell.c
LIBS     := gfx
include ../../host-test.mk

# the tests use the real fonts (tools/fonts/fetch.sh, from its cache)
FONT_DIR := $(abspath $(ROOT)/build/host-test/fonts)
CFLAGS   += -DFONT_DIR='"$(FONT_DIR)"'

run: $(FONT_DIR)/inter/Inter-Regular.ttf

$(FONT_DIR)/inter/Inter-Regular.ttf:
	$(ROOT)/tools/fonts/fetch.sh $(ROOT)/build/fonts $(FONT_DIR)
