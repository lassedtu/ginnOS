PROG := fetch
SRCS := fetch.c

# https when the mbedtls port is in the sysroot (it is built first)
SYSROOT := $(BUILD_ROOT)/sysroot
ifneq ($(wildcard $(SYSROOT)/usr/lib/libmbedtls.a),)
SRCS += tls.c
EXTRA_CFLAGS := -DFETCH_TLS -idirafter $(SYSROOT)/usr/include
EXTRA_LIBS := $(SYSROOT)/usr/lib/libmbedtls.a $(SYSROOT)/usr/lib/libmbedx509.a \
              $(SYSROOT)/usr/lib/libmbedcrypto.a
endif

include ../program.mk

# a new mbedtls library changes what fetch.c is compiled with
ifneq ($(filter tls.c,$(SRCS)),)
$(OBJS): $(SYSROOT)/usr/lib/libmbedtls.a
endif
