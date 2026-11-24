# ports: programs from other projects, built with the ginnOS toolchain
# (tools/toolchain/build.sh) and put into the image.
#
# a port is a script, ports/<name>/build.sh, called with the cross prefix,
# a source cache, a work folder and a root folder. it fetches a pinned
# source into the cache (build/sources/<name>, which make clean keeps),
# builds it in the work folder, and puts the files for the image into the
# root folder (bin/, usr/share/...). the image gets the root folders after
# our own programs; a file that both have stops the build.
#
# PORTS lists the ports to build. PORTS= builds none (the image then has no
# ls, cat and the other Unix tools, which sbase gives).

.PHONY: ports

PORTS         ?= sbase lua
TOOLCHAIN_BIN ?= $(abspath tools/toolchain/prefix/bin)
PORT_CROSS    := $(TOOLCHAIN_BIN)/$(ARCH)-ginnos-
PORTS_DIR     := $(BUILD_DIR)/ports
PORT_SOURCES  := $(abspath build/sources)
PORT_STAMPS   := $(foreach p,$(PORTS),$(PORTS_DIR)/$(p).done)
PORT_ROOTS    := $(foreach p,$(PORTS),$(PORTS_DIR)/$(p)/root)

ports: $(PORT_STAMPS)

# a port builds again when its script or the sysroot changes. its output
# goes to build/<arch>/ports/<name>.log; on an error make shows the end.
$(PORTS_DIR)/%.done: ports/%/build.sh $(SYSROOT_STAMP)
	@test -x $(PORT_CROSS)gcc || { \
		echo "the ports need the ginnOS toolchain ($(PORT_CROSS)gcc)."; \
		echo "build it once with tools/toolchain/build.sh $(ARCH) (about 20 minutes),"; \
		echo "or build without the ports: make PORTS="; \
		exit 1; }
	@mkdir -p $(PORTS_DIR)
	@echo "port: $*"
	+@MAKE="$(MAKE)" ./ports/$*/build.sh $(PORT_CROSS) $(PORT_SOURCES)/$* \
		$(abspath $(PORTS_DIR)/$*/work) $(abspath $(PORTS_DIR)/$*/root) \
		> $(PORTS_DIR)/$*.log 2>&1 || { \
		tail -n 20 $(PORTS_DIR)/$*.log; \
		echo "port $*: failed (the whole output is in $(PORTS_DIR)/$*.log)"; \
		exit 1; }
	@tail -n 1 $(PORTS_DIR)/$*.log
	@touch $@
