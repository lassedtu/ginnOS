# Makefile for doom (doomgeneric) in ginnOS userspace
#
# Builds the doomgeneric engine (a git submodule at src/user/doom/doomgeneric,
# not changed) with the ginnOS platform files doomgeneric_ginnos.c (screen,
# keys, time) and doomgeneric_ginnos_sound.c (sound effects on /dev/dsp).
# After cloning ginnOS, run: git submodule update --init

PROG := doom

ROOT     := ../../..
LIBC_INC := $(ROOT)/src/libc/include

# the top Makefile passes these for the architecture it builds
BUILD_ROOT     ?= $(ROOT)/build
ARCH_CFLAGS    ?= -m32
USER_LD_SCRIPT ?= $(ROOT)/linker/user.ld
TOOLCHAIN_FLAGS ?=

BUILD    := $(BUILD_ROOT)/user
DG_SRC   := doomgeneric/doomgeneric

CROSS   ?= i686-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld

# the engine is not our code: its own warnings are not ours to fix, so
# they are off for its files (our platform file keeps -Wall -Wextra)
ENGINE_WARNINGS := -w
CFLAGS  := -std=gnu99 -ffreestanding -O2 $(ARCH_CFLAGS) $(TOOLCHAIN_FLAGS) \
           -fno-pie -fno-stack-protector -nostdlib -fno-builtin-malloc \
           -I$(DG_SRC) -Iinclude -I$(LIBC_INC) -MMD -MP -DNORMALUNIX -D_DEFAULT_SOURCE -DFEATURE_SOUND

LDFLAGS := -T $(USER_LD_SCRIPT) -nostdlib

CRT0    := $(BUILD)/lib/crt0.o
LIBC    := $(BUILD_ROOT)/libc/libc.a
# gcc's helper functions (64-bit division on i686, for example)
LIBGCC  := $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name)

OBJDIR  := $(BUILD)/$(PROG)
TARGET  := $(BUILD)/bin/$(PROG)

# the engine files, as in doomgeneric's own Makefile, without a platform file
ENGINE_SRCS := dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c \
    d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c hu_lib.c \
    hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c i_system.c \
    i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c \
    m_menu.c m_misc.c m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c \
    p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c \
    p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c \
    r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c \
    st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c \
    w_wad.c z_zone.c w_file_stdc.c i_input.c i_video.c doomgeneric.c

ENGINE_OBJS := $(patsubst %.c,$(OBJDIR)/engine/%.o,$(ENGINE_SRCS))
PLAT_OBJS   := $(OBJDIR)/doomgeneric_ginnos.o $(OBJDIR)/doomgeneric_ginnos_sound.o
OBJS        := $(ENGINE_OBJS) $(PLAT_OBJS)

DEP_FILES := $(OBJS:.o=.d)

.PHONY: all
all: $(TARGET)

ifeq ($(wildcard $(DG_SRC)/doomgeneric.c),)
$(error doomgeneric submodule not found. Run: git submodule update --init)
endif

$(OBJDIR)/engine/%.o: $(DG_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ENGINE_WARNINGS) -c $< -o $@

# the ginnOS platform files (our code: full warnings)
$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -c $< -o $@

$(TARGET): $(OBJS) $(CRT0) $(LIBC)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIBC) $(LIBGCC)

-include $(DEP_FILES)
