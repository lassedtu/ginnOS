# run ginnOS in QEMU, and debug it with gdb

.PHONY: run run-uefi debug gdb

# the sound card for make run and run-uefi: an Intel HDA controller with a
# line out, played through the host's sound system (coreaudio on macOS,
# pa on Linux). AUDIO=ac97 uses an AC'97 card instead, AUDIO=0 none, and
# QEMU_AUDIODEV=<backend> picks another backend (alsa, sdl, wav,path=x.wav).
AUDIO ?= hda
ifeq ($(shell uname -s),Darwin)
QEMU_AUDIODEV ?= coreaudio
else
QEMU_AUDIODEV ?= pa
endif
ifeq ($(AUDIO),hda)
QEMU_AUDIO_FLAGS := -audiodev $(QEMU_AUDIODEV),id=snd0 -device intel-hda -device hda-output,audiodev=snd0
else ifeq ($(AUDIO),ac97)
QEMU_AUDIO_FLAGS := -audiodev $(QEMU_AUDIODEV),id=snd0 -device AC97,audiodev=snd0
else
QEMU_AUDIO_FLAGS :=
endif

run: check-tools $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) $(QEMU_AUDIO_FLAGS) -drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

# boot the disk image with UEFI firmware (limine only: stage1 is BIOS-only).
# the i686 kernel also runs on an x86_64 CPU, so this uses the x86_64
# firmware for both.
run-uefi: check-tools $(DISK_IMAGE)
	@test "$(BOOTLOADER)" = limine || { echo "make run-uefi needs BOOTLOADER=limine"; exit 1; }
	@test -n "$(OVMF_CODE)" || { echo "no OVMF firmware found: set OVMF_CODE=<path to OVMF_CODE.fd>"; exit 1; }
	$(QEMU_UEFI) $(QEMU_FLAGS) $(QEMU_AUDIO_FLAGS) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=ide,index=0,format=raw,file=$(DISK_IMAGE)

# debug the kernel: boot QEMU halted at the first instruction with a gdb stub
# on GDB_PORT, attach gdb with build/kernel.elf and tools/gdb/ginnos.gdb
# (breakpoints on panics), and stop QEMU when gdb exits. type `c` to boot.
gdb: check-tools $(DISK_IMAGE)
	@command -v $(GDB) >/dev/null 2>&1 || { echo "missing tool: $(GDB) (set GDB=...)"; exit 1; }
	@PYTHON="$(PYTHON)" ./tools/gdb/run_gdb.sh "$(QEMU)" "$(DISK_IMAGE)" "$(KERNEL_ELF)" \
		"$(GDB)" "$(GDB_PORT)" $(QEMU_FLAGS) -- $(GDB_ARGS)

# same as make gdb, but with the kernel built -Og -DDEBUG (easier stepping,
# fewer optimised-out variables, debug-level klog).
debug:
	$(MAKE) BUILD_MODE=debug gdb
