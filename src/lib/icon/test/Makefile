TEST     := icon
SRCS     := icon_test.c
LIB_SRCS := ../icon.c
LIBS     := image gfx
include ../../host-test.mk

# the tests read the real packs (make icons) and the icon lists
PACK_DIR := $(abspath $(ROOT)/build/icons/out)
CFLAGS   += -DPACK_DIR='"$(PACK_DIR)"' -DLIST_DIR='"$(abspath $(ROOT)/tools/icons)"'

run: $(PACK_DIR)/tela.pack

$(PACK_DIR)/tela.pack:
	$(MAKE) -C $(ROOT) icons
