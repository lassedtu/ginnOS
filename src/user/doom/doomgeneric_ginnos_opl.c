/**
 * @file doomgeneric_ginnos_opl.c
 * @brief the OPL library interface (opl.h) for Doom's music on ginnOS.
 *
 * Doom's music is MUS data played on an OPL2/OPL3 FM chip. Chocolate
 * Doom's i_oplmusic.c (the music player, a submodule at chocolate-doom)
 * talks to the chip through opl.h: register writes, and callbacks at given
 * times (the next note). Chocolate Doom implements opl.h over SDL; this
 * file implements it over the Nuked OPL3 emulator (opl3.c) and the
 * callback queue (opl_queue.c), both from the same submodule, in the way
 * of Chocolate Doom's opl_sdl.c.
 *
 * there is no thread and no clock of its own: the sound module calls
 * opl_ginnos_generate when it mixes, and the music time moves with the
 * samples made. a callback runs when its time is reached in the stream,
 * so the notes keep time with the sound output.
 */

#include "doomgeneric_ginnos_opl.h"

#include "opl.h"
#include "opl3.h"
#include "opl_queue.h"

#include <stdint.h>
#include <stdlib.h>

static opl3_chip chip;
static opl_callback_queue_t *queue;
static unsigned int sample_rate = 44100;
static int running;

static uint64_t current_time; // microseconds of sound made so far
static uint64_t pause_offset; // time spent paused
static int paused;

// Dehacked is not built into doomgeneric, where DEH_String is a macro;
// Chocolate Doom's headers call it as a function. no text is replaced.
char *DEH_String(char *s)
{
    return s;
}

void OPL_SetSampleRate(unsigned int rate)
{
    sample_rate = rate;
}

opl_init_result_t OPL_Init(unsigned int port_base)
{
    (void)port_base; // an emulator: no I/O port

    queue = OPL_Queue_Create();
    if (!queue)
    {
        return OPL_INIT_NONE;
    }
    OPL3_Reset(&chip, sample_rate);
    current_time = 0;
    pause_offset = 0;
    paused = 0;
    running = 1;
    return OPL_INIT_OPL3;
}

void OPL_Shutdown(void)
{
    if (running)
    {
        OPL_Queue_Destroy(queue);
        queue = NULL;
        running = 0;
    }
}

void OPL_WriteRegister(int reg, int value)
{
    // the timer registers matter only for the detection of a real chip
    if (!running || reg == OPL_REG_TIMER1 || reg == OPL_REG_TIMER2 ||
        reg == OPL_REG_TIMER_CTRL)
    {
        return;
    }
    OPL3_WriteRegBuffered(&chip, (Bit16u)reg, (Bit8u)value);
}

void OPL_InitRegisters(int opl3)
{
    // the same writes as Doom (and Chocolate Doom's opl.c) at startup
    for (int bank = 0; bank <= (opl3 ? 1 : 0); bank++)
    {
        int high = bank ? 0x100 : 0;
        if (bank)
        {
            OPL_WriteRegister(OPL_REG_NEW, 0x01);
        }
        for (int r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; r++)
        {
            OPL_WriteRegister(r | high, 0x3F); // all operators silent
        }
        for (int r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; r++)
        {
            OPL_WriteRegister(r | high, 0x00);
        }
        for (int r = 1; r < OPL_REGS_LEVEL; r++)
        {
            OPL_WriteRegister(r | high, 0x00);
        }
        if (!bank)
        {
            OPL_WriteRegister(OPL_REG_WAVEFORM_ENABLE, 0x20);
        }
    }
    OPL_WriteRegister(OPL_REG_FM_MODE, 0x40);
    if (opl3)
    {
        OPL_WriteRegister(OPL_REG_NEW, 0x01);
    }
}

void OPL_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{
    if (running)
    {
        OPL_Queue_Push(queue, callback, data, current_time - pause_offset + us);
    }
}

void OPL_ClearCallbacks(void)
{
    if (running)
    {
        OPL_Queue_Clear(queue);
    }
}

void OPL_AdjustCallbacks(float factor)
{
    if (running)
    {
        OPL_Queue_AdjustCallbacks(queue, current_time, factor);
    }
}

void OPL_SetPaused(int pause)
{
    paused = pause;
}

// one process, no threads: the callbacks run inside opl_ginnos_generate,
// never at the same time as the music player's own calls
void OPL_Lock(void)
{
}

void OPL_Unlock(void)
{
}

/**
 * run the callbacks whose time has come.
 */
static void run_callbacks(void)
{
    opl_callback_t callback;
    void *data;
    while (!OPL_Queue_IsEmpty(queue) && current_time >= OPL_Queue_Peek(queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(queue, &callback, &data))
        {
            break;
        }
        callback(data);
    }
}

void opl_ginnos_generate(int16_t *out, unsigned int frames)
{
    if (!running)
    {
        for (unsigned int i = 0; i < frames * 2; i++)
        {
            out[i] = 0;
        }
        return;
    }

    unsigned int done = 0;
    while (done < frames)
    {
        // make sound up to the next callback (or to the end)
        uint64_t count = frames - done;
        if (!paused && !OPL_Queue_IsEmpty(queue))
        {
            uint64_t next = OPL_Queue_Peek(queue) + pause_offset;
            uint64_t until = next > current_time ? next - current_time : 0;
            uint64_t samples = (until * sample_rate + OPL_SECOND - 1) / OPL_SECOND;
            if (samples < count)
            {
                count = samples;
            }
        }

        if (count > 0)
        {
            OPL3_GenerateStream(&chip, out + done * 2, (Bit32u)count);
            done += (unsigned int)count;
            uint64_t us = count * OPL_SECOND / sample_rate;
            current_time += us;
            if (paused)
            {
                pause_offset += us;
            }
        }
        run_callbacks();
    }
}
