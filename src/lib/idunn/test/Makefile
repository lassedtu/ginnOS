TEST     := idunn
SRCS     := idunn_test.c widgets_test.c inputs_test.c lists_test.c
LIB_SRCS := ../theme.c ../context.c ../widget.c ../layout.c ../window.c ../label.c \
            ../button.c ../separator.c ../text_edit.c ../text_field.c ../text_area.c ../option.c ../check.c ../radio.c ../switch.c ../slider.c ../field.c ../scroll.c
LIBS     := conf gfx bragi icon image
include ../../host-test.mk

# the tests use the real fonts and icons (from their caches)
FONT_DIR := $(abspath $(ROOT)/build/host-test/fonts)
PACK_DIR := $(abspath $(ROOT)/build/icons/out)
CFLAGS   += -DFONT_DIR='"$(FONT_DIR)"' -DPACK_DIR='"$(PACK_DIR)"'

run: $(FONT_DIR)/inter/Inter-Regular.ttf $(PACK_DIR)/lucide.pack

$(FONT_DIR)/inter/Inter-Regular.ttf:
	$(ROOT)/tools/fonts/fetch.sh $(ROOT)/build/fonts $(FONT_DIR)

$(PACK_DIR)/lucide.pack:
	$(MAKE) -C $(ROOT) icons
