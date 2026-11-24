# the root filesystem and the disk image

.PHONY: rootfs-image icons

# root filesystem / disk image
EXT2_IMAGE_TOOL  := tools/ext2/make_image.py
EXT2_SOURCE_DIR  ?= tools/ext2/rootfs
# the files of the root filesystem: a copy of EXT2_SOURCE_DIR with the
# programs (and, for stage2, kernel.bin), one per architecture
ROOTFS_STAGING   := $(BUILD_DIR)/rootfs
EXT2_SIZE_MB     ?=
EXT2_MIN_SIZE_MB ?= 8

# root filesystem and disk image
# stage2 loads /boot/kernel.bin from the root filesystem (i686 only)
ifeq ($(ARCH),i686)
ROOTFS_KERNEL := $(KERNEL_BIN)
endif

# both packs come from one run of build.py
$(ICONS_OUT)/tela.pack: $(ICONS_OUT)/lucide.pack
$(ICONS_OUT)/lucide.pack: tools/icons/build.py tools/icons/fetch.sh tools/icons/lucide.list \
                          tools/icons/tela.list
	./tools/icons/fetch.sh $(ICONS_CACHE)
	$(PYTHON) -I tools/icons/build.py $(ICONS_CACHE)/src $(ICONS_OUT)

icons: $(ICONS_PACKS)

$(WALLPAPER_OUT): $(wildcard $(WALLPAPER)) tools/wallpaper/make.py
	$(PYTHON) tools/wallpaper/make.py "$(WALLPAPER)" $@ $(RESOLUTION)

rootfs-image: $(ROOTFS_KERNEL) user-programs $(PORT_STAMPS) $(if $(filter 1,$(ICONS)),$(ICONS_PACKS)) \
              $(if $(WALLPAPER),$(WALLPAPER_OUT))
	rm -rf $(ROOTFS_STAGING)
	@mkdir -p $(ROOTFS_STAGING) $(dir $(ROOTFS_IMAGE))
	cp -R $(EXT2_SOURCE_DIR)/. $(ROOTFS_STAGING)/
	@mkdir -p $(ROOTFS_STAGING)/boot $(ROOTFS_STAGING)/bin
	$(if $(ROOTFS_KERNEL),cp $(ROOTFS_KERNEL) $(ROOTFS_STAGING)/boot/kernel.bin)
	@for prog in $(USER_PROGRAMS); do \
		cp $$prog $(ROOTFS_STAGING)/bin/$$(basename $$prog); \
	done
	@for root in $(PORT_ROOTS); do \
		for f in $$(cd $$root && find . ! -type d); do \
			if [ -e $(ROOTFS_STAGING)/$$f ] || [ -L $(ROOTFS_STAGING)/$$f ]; then \
				echo "$${f#.} is in a port ($$root) and in ginnOS"; exit 1; \
			fi; \
		done; \
		cp -R $$root/. $(ROOTFS_STAGING)/; \
	done
	$(PYTHON) tools/sounds/make_sounds.py $(ROOTFS_STAGING)/usr/share/sounds
	$(if $(filter 1,$(FREEDOOM)),./$(FREEDOOM_FETCH) $(FREEDOOM_CACHE) $(ROOTFS_STAGING)/usr/share/doom)
	$(if $(filter 1,$(FONTS)),./$(FONTS_FETCH) $(FONTS_CACHE) $(ROOTFS_STAGING)/usr/share/fonts)
	$(if $(filter 1,$(ICONS)),mkdir -p $(ROOTFS_STAGING)/usr/share/icons && \
		cp $(ICONS_PACKS) $(ICONS_OUT)/LICENSE-lucide.txt $(ICONS_OUT)/COPYING-tela.txt \
		$(ROOTFS_STAGING)/usr/share/icons/)
	@if [ -n "$(WALLPAPER)" ] && [ -f $(WALLPAPER_OUT) ]; then \
		mkdir -p $(ROOTFS_STAGING)/usr/share/wallpapers; \
		cp $(WALLPAPER_OUT) $(ROOTFS_STAGING)/usr/share/wallpapers/default.png; \
	fi
	$(if $(filter 1,$(DOOM)),cp src/user/doom/doom.app $(ROOTFS_STAGING)/usr/share/applications/)
	@if [ "$(DOOM)" = 1 ] && [ -d "$(DOOM_WAD_DIR)" ]; then \
		for wad in "$(DOOM_WAD_DIR)"/*.[wW][aA][dD]; do \
			[ -f "$$wad" ] || continue; \
			mkdir -p $(ROOTFS_STAGING)/usr/share/doom; \
			name=$$(basename "$$wad" | tr 'A-Z' 'a-z'); \
			echo "doom: $$wad -> /usr/share/doom/$$name"; \
			cp "$$wad" "$(ROOTFS_STAGING)/usr/share/doom/$$name"; \
		done; \
	fi
	$(PYTHON) $(EXT2_IMAGE_TOOL) \
		--source "$(ROOTFS_STAGING)" \
		--output "$(ROOTFS_IMAGE)" \
		--min-size-mb $(EXT2_MIN_SIZE_MB) \
		$(if $(EXT2_SIZE_MB),--size-mb $(EXT2_SIZE_MB),)

ifeq ($(BOOTLOADER),limine)
$(DISK_IMAGE): $(KERNEL_ELF) rootfs-image
	$(call limine_disk,$@,$(KERNEL_ELF),$(ROOTFS_IMAGE),$(BUILD_DIR)/boot/limine)
else
$(DISK_IMAGE): $(STAGE1_BIN) $(STAGE2_PAD) rootfs-image
	$(call assemble_disk,$@,$(ROOTFS_IMAGE))
endif
