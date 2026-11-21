/**
 * the PS/2 mouse driver (the i8042 aux port; the IntelliMouse wheel
 * extension).
 *
 * setup: turn on the aux port and its interrupt in the controller's
 * configuration byte, reset the mouse to its defaults, ask for the wheel
 * with the sample rate sequence 200, 100, 80 (a wheel mouse then reports
 * id 3 and sends 4-byte packets), and turn on data reporting.
 *
 * a packet: byte 0 has the buttons (bits 0 to 2), a bit that is always 1
 * (bit 3), and the sign bits of x and y (bits 4 and 5); bytes 1 and 2 are
 * the movement; byte 3 is the wheel. y and the wheel count up when the
 * mouse moves away from the user, so both change sign for /dev/input.
 */

#include "ps2_mouse.h"

#include "arch/x86/cpu/io.h"
#include "kernel/input/input.h"
#include "kernel/irq/irq.h"
#include "kernel/klog/klog.h"

#define DATA_PORT    0x60
#define STATUS_PORT  0x64
#define COMMAND_PORT 0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_AUX_DATA    0x20

#define CTRL_READ_CONFIG  0x20
#define CTRL_WRITE_CONFIG 0x60
#define CTRL_ENABLE_AUX   0xA8
#define CTRL_WRITE_AUX    0xD4

#define CONFIG_AUX_IRQ     0x02
#define CONFIG_AUX_DISABLE 0x20

#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE       0xF4
#define MOUSE_SAMPLE_RATE  0xF3
#define MOUSE_GET_ID       0xF2
#define MOUSE_ACK          0xFA

#define PACKET_ALWAYS_ONE  0x08
#define PACKET_X_SIGN      0x10
#define PACKET_Y_SIGN      0x20
#define PACKET_OVERFLOW    0xC0

#define SPIN_LIMIT 1000000u

static uint8_t packet[4];
static uint32_t packet_used;
static uint32_t packet_size = 3;
static uint8_t buttons;
static bool present;

static bool wait_write(void)
{
    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        if (!(io_inb(STATUS_PORT) & STATUS_INPUT_FULL))
        {
            return true;
        }
    }
    return false;
}

static bool wait_read(void)
{
    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        if (io_inb(STATUS_PORT) & STATUS_OUTPUT_FULL)
        {
            return true;
        }
    }
    return false;
}

static bool controller_command(uint8_t command)
{
    if (!wait_write())
    {
        return false;
    }
    io_outb(COMMAND_PORT, command);
    return true;
}

/**
 * send a byte to the mouse and wait for its acknowledgement.
 */
static bool mouse_send(uint8_t byte)
{
    if (!controller_command(CTRL_WRITE_AUX) || !wait_write())
    {
        return false;
    }
    io_outb(DATA_PORT, byte);
    return wait_read() && io_inb(DATA_PORT) == MOUSE_ACK;
}

static bool set_sample_rate(uint8_t rate)
{
    return mouse_send(MOUSE_SAMPLE_RATE) && mouse_send(rate);
}

static void report_button(uint8_t changed, uint8_t mask, uint16_t code)
{
    if (changed & mask)
    {
        input_report(EV_KEY, code, (buttons & mask) ? 1 : 0);
    }
}

static void handle_packet(void)
{
    uint8_t flags = packet[0];
    if (flags & PACKET_OVERFLOW)
    {
        return; // the movement does not fit: drop it
    }

    int32_t dx = packet[1] - ((flags & PACKET_X_SIGN) ? 256 : 0);
    int32_t dy = packet[2] - ((flags & PACKET_Y_SIGN) ? 256 : 0);
    int32_t wheel = packet_size == 4 ? (int8_t)packet[3] : 0;

    uint8_t now = flags & 0x07;
    uint8_t changed = now ^ buttons;
    buttons = now;

    report_button(changed, 0x01, BTN_LEFT);
    report_button(changed, 0x02, BTN_RIGHT);
    report_button(changed, 0x04, BTN_MIDDLE);
    if (dx)
    {
        input_report(EV_REL, REL_X, dx);
    }
    if (dy)
    {
        input_report(EV_REL, REL_Y, -dy);
    }
    if (wheel)
    {
        input_report(EV_REL, REL_WHEEL, -wheel);
    }
    if (changed || dx || dy || wheel)
    {
        input_sync();
    }
}

void ps2_mouse_byte(uint8_t byte)
{
    if (!present)
    {
        return;
    }
    // byte 0 always has bit 3 set: skip bytes until the packets line up
    if (packet_used == 0 && !(byte & PACKET_ALWAYS_ONE))
    {
        return;
    }
    packet[packet_used++] = byte;
    if (packet_used == packet_size)
    {
        packet_used = 0;
        handle_packet();
    }
}

static void mouse_interrupt(uint32_t irq, trap_frame_t *frame)
{
    (void)irq;
    (void)frame;
    uint8_t status = io_inb(STATUS_PORT);
    if ((status & STATUS_OUTPUT_FULL) && (status & STATUS_AUX_DATA))
    {
        ps2_mouse_byte(io_inb(DATA_PORT));
    }
}

bool ps2_mouse_init(void)
{
    // a controller that is not there reads as 0xFF
    if (io_inb(STATUS_PORT) == 0xFF || !controller_command(CTRL_ENABLE_AUX))
    {
        return false;
    }

    // the aux interrupt on, the aux clock on
    if (!controller_command(CTRL_READ_CONFIG) || !wait_read())
    {
        return false;
    }
    uint8_t config = io_inb(DATA_PORT);
    config = (uint8_t)((config | CONFIG_AUX_IRQ) & ~CONFIG_AUX_DISABLE);
    if (!controller_command(CTRL_WRITE_CONFIG) || !wait_write())
    {
        return false;
    }
    io_outb(DATA_PORT, config);

    if (!mouse_send(MOUSE_SET_DEFAULTS))
    {
        KLOG_INFO("ps2 mouse: none");
        return false;
    }

    // the IntelliMouse sequence: a wheel mouse then answers id 3
    if (set_sample_rate(200) && set_sample_rate(100) && set_sample_rate(80) &&
        mouse_send(MOUSE_GET_ID) && wait_read() && io_inb(DATA_PORT) == 3)
    {
        packet_size = 4;
    }
    set_sample_rate(100);

    if (!mouse_send(MOUSE_ENABLE))
    {
        return false;
    }

    present = true;
    irq_request(12, mouse_interrupt, IRQ_FLAG_NONE, "mouse");
    KLOG_INFO("ps2 mouse: %s", packet_size == 4 ? "with a wheel" : "3 buttons");
    return true;
}
