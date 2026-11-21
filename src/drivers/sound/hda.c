/**
 * the Intel HDA driver (High Definition Audio Specification 1.0a).
 *
 * controller: the host sends codec commands (verbs) through the command
 * output ring buffer (CORB) and reads the answers from the response input
 * ring buffer (RIRB), both in RAM. a stream descriptor plays a cyclic
 * buffer described by a buffer descriptor list (BDL).
 *
 * codec: a tree of widgets (nodes). an output path goes from a pin
 * complex (the jack or the speaker) through mixers and selectors to an
 * audio output converter (DAC). the driver finds such a path for each
 * connected output pin of the first codec that has one, powers the
 * widgets, unmutes their amplifiers, selects the connections and sets the
 * converters to stream 1. all the converters take the same stream, so the
 * sound plays on every output.
 *
 * playback: PERIODS buffers of PERIOD_BYTES in one cyclic buffer, with an
 * interrupt after each. the stream loops over the buffer by itself; when
 * the position (LPIB) has passed a period, the driver fills it again from
 * the audio core. the refill also runs from the timer, in case the
 * interrupt does not come.
 *
 * the commands are polled; they are sent only during setup and for volume
 * changes.
 */

#include "hda.h"

#include "arch/x86/cpu/io.h"
#include "common/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_irq.h"
#include "kernel/audio/audio.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"
#include "kernel/time/timer.h"

/* ---- controller registers ------------------------------------------------ */

#define GCAP      0x00 // 16 bits
#define GCTL      0x08
#define STATESTS  0x0E // 16 bits
#define INTCTL    0x20
#define INTSTS    0x24
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP    0x48 // 16 bits
#define CORBRP    0x4A // 16 bits
#define CORBCTL   0x4C // 8 bits
#define CORBSIZE  0x4E // 8 bits
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP    0x58 // 16 bits
#define RINTCNT   0x5A // 16 bits
#define RIRBCTL   0x5C // 8 bits
#define RIRBSTS   0x5D // 8 bits
#define RIRBSIZE  0x5E // 8 bits

#define GCAP_64OK      0x0001u
#define GCTL_CRST      0x00000001u
#define INTCTL_GIE     0x80000000u
#define CORBRP_RESET   0x8000u
#define RIRBWP_RESET   0x8000u
#define RING_RUN       0x02u // CORBCTL and RIRBCTL: DMA on
#define RIRBSTS_CLEAR  0x05u // response interrupt and overrun (write 1 to clear)

// stream descriptor registers (offsets in a descriptor)
#define SD_BASE  0x80
#define SD_SIZE  0x20
#define SD_CTL0  0x00 // 8 bits: reset, run, interrupt enables
#define SD_CTL2  0x02 // 8 bits: the stream tag in bits 4 to 7
#define SD_STS   0x03 // 8 bits
#define SD_LPIB  0x04
#define SD_CBL   0x08
#define SD_LVI   0x0C // 16 bits
#define SD_FMT   0x12 // 16 bits
#define SD_BDPL  0x18
#define SD_BDPU  0x1C

#define SD_SRST   0x01u
#define SD_RUN    0x02u
#define SD_IOCE   0x04u
#define SD_BCIS   0x04u // status: buffer completed
#define SD_STS_CLEAR 0x1Cu // BCIS, FIFOE, DESE

/* ---- codec verbs and parameters ------------------------------------------ */

#define VERB_GET_PARAM      0xF00
#define VERB_GET_CONN       0xF02
#define VERB_SET_SELECT     0x701
#define VERB_SET_POWER      0x705
#define VERB_SET_STREAM     0x706
#define VERB_SET_PIN_CTL    0x707
#define VERB_SET_EAPD       0x70C
#define VERB_GET_PIN_SENSE  0xF09
#define VERB_GET_CONFIG     0xF1C
#define VERB4_SET_FORMAT    0x2 // 4-bit verbs (16-bit payload)
#define VERB4_SET_AMP       0x3

#define PARAM_VENDOR       0x00
#define PARAM_NODE_COUNT   0x04
#define PARAM_FG_TYPE      0x05
#define PARAM_WIDGET_CAPS  0x09
#define PARAM_PCM          0x0A
#define PARAM_PIN_CAPS     0x0C
#define PARAM_IN_AMP_CAPS  0x0D
#define PARAM_CONN_LEN     0x0E
#define PARAM_OUT_AMP_CAPS 0x12

#define FG_AUDIO 0x01

#define WIDGET_TYPE(caps)  (((caps) >> 20) & 0xFu)
#define WIDGET_DAC      0x0
#define WIDGET_MIXER    0x2
#define WIDGET_SELECTOR 0x3
#define WIDGET_PIN      0x4
#define CAPS_IN_AMP     (1u << 1)
#define CAPS_OUT_AMP    (1u << 2)
#define CAPS_AMP_OVERRIDE (1u << 3)

#define PIN_CAPS_PRESENCE (1u << 2)
#define PIN_CAPS_OUTPUT   (1u << 4)
#define PIN_CAPS_EAPD     (1u << 16)

#define CONFIG_PORT(c)    (((c) >> 30) & 0x3u) // 1 = no physical connection
#define CONFIG_DEVICE(c)  (((c) >> 20) & 0xFu)
#define DEVICE_LINE_OUT   0x0
#define DEVICE_SPEAKER    0x1
#define DEVICE_HEADPHONE  0x2

#define PIN_CTL_OUT       0x40u
#define PIN_CTL_HP        0x80u
#define EAPD_ON           0x02u
#define AMP_OUT           0x8000u
#define AMP_IN            0x4000u
#define AMP_LEFT_RIGHT    0x3000u
#define AMP_MUTE          0x0080u

/* ---- driver limits ------------------------------------------------------- */

#define MAX_NODES   128u
#define MAX_CONN    16u
#define MAX_PATH    5u
#define MAX_PATHS   4u
#define STREAM_TAG  1u
#define PERIODS     4u
#define PERIOD_BYTES 2048u
#define SPIN_LIMIT  2000000u

typedef struct
{
    uint8_t nodes[MAX_PATH]; // pin first, DAC last
    uint8_t length;
    uint8_t device;          // DEVICE_ of the pin
} path_t;

typedef struct
{
    uint64_t address;
    uint32_t length;
    uint32_t flags; // bit 0: interrupt on completion
} __attribute__((packed)) bdl_entry_t;

static volatile uint8_t *regs;
static volatile uint32_t *corb;
static volatile uint32_t *rirb;
static uint32_t corb_entries;
static uint32_t rirb_entries;
static uint32_t rirb_read;
static uint8_t *dma;
static phys_addr_t dma_phys;
static volatile uint8_t *sd; // the output stream descriptor
static uint32_t stream_index; // its bit in INTCTL and INTSTS

static uint32_t codec;
static uint32_t widget_caps[MAX_NODES];
static uint8_t conns[MAX_NODES][MAX_CONN];
static uint8_t conn_count[MAX_NODES];
static uint32_t afg;            // the audio function group node
static uint32_t afg_out_amp;    // its default output amp caps
static uint32_t afg_in_amp;
static path_t paths[MAX_PATHS];
static uint32_t path_count;

static uint16_t stream_format;
static volatile bool playing;
static uint32_t fill_period; // the next period to fill
static audio_device_t audio;

#define DMA_CORB    0x0000u
#define DMA_RIRB    0x0800u
#define DMA_BDL     0x1000u
#define DMA_BUFFERS 0x2000u
#define DMA_PAGES   ((DMA_BUFFERS + PERIODS * PERIOD_BYTES + PAGE_SIZE - 1) / PAGE_SIZE)

/* ---- register access ----------------------------------------------------- */

static uint32_t rd32(uint32_t reg)
{
    return mmio_read32(regs + reg);
}

static void wr32(uint32_t reg, uint32_t value)
{
    mmio_write32(regs + reg, value);
}

static uint16_t rd16(uint32_t reg)
{
    return mmio_read16(regs + reg);
}

static void wr16(uint32_t reg, uint16_t value)
{
    mmio_write16(regs + reg, value);
}

static uint8_t rd8(uint32_t reg)
{
    return mmio_read8(regs + reg);
}

static void wr8(uint32_t reg, uint8_t value)
{
    mmio_write8(regs + reg, value);
}

/**
 * about one microsecond (a write to the POST port).
 */
static void delay_us(uint32_t us)
{
    for (uint32_t i = 0; i < us; i++)
    {
        io_outb(0x80, 0);
    }
}

/* ---- commands ------------------------------------------------------------ */

/**
 * send one verb and wait for its answer.
 * @param verb the whole 32-bit command.
 * @param response receives the answer.
 * @return false on a timeout.
 */
static bool command(uint32_t verb, uint32_t *response)
{
    uint32_t write = ((rd16(CORBWP) & 0xFFu) + 1) % corb_entries;
    corb[write] = verb;
    wr16(CORBWP, (uint16_t)write);

    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        if ((rd16(RIRBWP) & 0xFFu) == rirb_read)
        {
            continue;
        }
        rirb_read = (rirb_read + 1) % rirb_entries;
        uint32_t answer = rirb[rirb_read * 2];
        uint32_t extended = rirb[rirb_read * 2 + 1];

        // some controllers (QEMU's among them) stop taking commands after
        // RINTCNT responses until this status is cleared
        wr8(RIRBSTS, RIRBSTS_CLEAR);
        if (extended & 0x10u)
        {
            continue; // an unsolicited response (a jack event), not ours
        }
        *response = answer;
        return true;
    }
    return false;
}

static uint32_t verb12(uint32_t node, uint32_t verb, uint32_t payload)
{
    uint32_t response = 0;
    command((codec << 28) | (node << 20) | (verb << 8) | (payload & 0xFFu), &response);
    return response;
}

static uint32_t verb4(uint32_t node, uint32_t verb, uint32_t payload)
{
    uint32_t response = 0;
    command((codec << 28) | (node << 20) | (verb << 16) | (payload & 0xFFFFu), &response);
    return response;
}

static uint32_t param(uint32_t node, uint32_t which)
{
    return verb12(node, VERB_GET_PARAM, which);
}

/* ---- controller setup ---------------------------------------------------- */

static bool reset_controller(void)
{
    wr32(GCTL, rd32(GCTL) & ~GCTL_CRST);
    for (uint32_t spin = 0; rd32(GCTL) & GCTL_CRST; spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            return false;
        }
    }
    delay_us(100);
    wr32(GCTL, rd32(GCTL) | GCTL_CRST);
    for (uint32_t spin = 0; !(rd32(GCTL) & GCTL_CRST); spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            return false;
        }
    }
    // the codecs announce themselves within 521 us of the reset
    delay_us(1000);
    return true;
}

/**
 * the largest ring size the controller offers (its size capability bits
 * 4 to 6 in CORBSIZE or RIRBSIZE): the size code and the
 * number of entries.
 */
static uint8_t ring_size(uint8_t caps, uint32_t *entries)
{
    if (caps & 0x40u)
    {
        *entries = 256;
        return 2;
    }
    if (caps & 0x20u)
    {
        *entries = 16;
        return 1;
    }
    *entries = 2;
    return 0;
}

static bool setup_rings(void)
{
    phys_addr_t corb_phys = dma_phys + DMA_CORB;
    phys_addr_t rirb_phys = dma_phys + DMA_RIRB;

    wr8(CORBCTL, 0);
    wr8(RIRBCTL, 0);
    for (uint32_t spin = 0; (rd8(CORBCTL) | rd8(RIRBCTL)) & RING_RUN; spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            return false;
        }
    }

    wr8(CORBSIZE, ring_size(rd8(CORBSIZE), &corb_entries) | (rd8(CORBSIZE) & 0xF0u));
    wr32(CORBLBASE, (uint32_t)corb_phys);
    wr32(CORBUBASE, (uint32_t)((uint64_t)corb_phys >> 32));
    wr16(CORBRP, CORBRP_RESET);
    for (uint32_t spin = 0; !(rd16(CORBRP) & CORBRP_RESET) && spin < SPIN_LIMIT; spin++)
    {
    }
    wr16(CORBRP, 0);
    for (uint32_t spin = 0; (rd16(CORBRP) & CORBRP_RESET) && spin < SPIN_LIMIT; spin++)
    {
    }
    wr16(CORBWP, 0);

    wr8(RIRBSIZE, ring_size(rd8(RIRBSIZE), &rirb_entries) | (rd8(RIRBSIZE) & 0xF0u));
    wr32(RIRBLBASE, (uint32_t)rirb_phys);
    wr32(RIRBUBASE, (uint32_t)((uint64_t)rirb_phys >> 32));
    wr16(RIRBWP, RIRBWP_RESET);
    wr16(RINTCNT, 0xFF);
    rirb_read = 0;

    wr8(CORBCTL, RING_RUN);
    wr8(RIRBCTL, RING_RUN);
    return true;
}

/* ---- codec discovery ----------------------------------------------------- */

/**
 * read the connection list of a node (the inputs of a mixer or selector,
 * the source of a pin). ranges in the long form are not supported.
 */
static void read_connections(uint32_t node)
{
    uint32_t info = param(node, PARAM_CONN_LEN);
    uint32_t length = info & 0x7Fu;
    bool long_form = (info & 0x80u) != 0;
    uint32_t per_answer = long_form ? 2 : 4;
    uint32_t bits = long_form ? 16 : 8;

    conn_count[node] = 0;
    for (uint32_t i = 0; i < length && conn_count[node] < MAX_CONN; i += per_answer)
    {
        uint32_t answer = verb12(node, VERB_GET_CONN, i);
        for (uint32_t j = 0; j < per_answer && i + j < length; j++)
        {
            uint32_t entry = (answer >> (j * bits)) & ((1u << bits) - 1);
            uint32_t id = entry & (long_form ? 0x7FFFu : 0x7Fu);
            if (id < MAX_NODES && conn_count[node] < MAX_CONN)
            {
                conns[node][conn_count[node]++] = (uint8_t)id;
            }
        }
    }
}

/**
 * depth-first search from a node to a DAC. the path is filled from the
 * pin (index 0) to the DAC.
 */
static bool find_dac(uint32_t node, path_t *path)
{
    if (path->length >= MAX_PATH)
    {
        return false;
    }
    path->nodes[path->length++] = (uint8_t)node;

    uint32_t type = WIDGET_TYPE(widget_caps[node]);
    if (type == WIDGET_DAC)
    {
        return true;
    }
    if (path->length == 1 || type == WIDGET_MIXER || type == WIDGET_SELECTOR)
    {
        for (uint32_t i = 0; i < conn_count[node]; i++)
        {
            if (find_dac(conns[node][i], path))
            {
                return true;
            }
        }
    }
    path->length--;
    return false;
}

/**
 * look at an audio function group: read the widgets and find the output
 * paths.
 */
static void scan_function_group(uint32_t node)
{
    uint32_t count = param(node, PARAM_NODE_COUNT);
    uint32_t first = (count >> 16) & 0xFFu;
    uint32_t total = count & 0xFFu;

    afg = node;
    afg_out_amp = param(node, PARAM_OUT_AMP_CAPS);
    afg_in_amp = param(node, PARAM_IN_AMP_CAPS);
    verb12(node, VERB_SET_POWER, 0); // D0

    for (uint32_t n = first; n < first + total && n < MAX_NODES; n++)
    {
        widget_caps[n] = param(n, PARAM_WIDGET_CAPS);
        read_connections(n);
    }

    for (uint32_t n = first; n < first + total && n < MAX_NODES && path_count < MAX_PATHS; n++)
    {
        if (WIDGET_TYPE(widget_caps[n]) != WIDGET_PIN ||
            !(param(n, PARAM_PIN_CAPS) & PIN_CAPS_OUTPUT))
        {
            continue;
        }
        uint32_t config = verb12(n, VERB_GET_CONFIG, 0);
        uint32_t device = CONFIG_DEVICE(config);
        if (CONFIG_PORT(config) == 1 ||
            (device != DEVICE_LINE_OUT && device != DEVICE_SPEAKER && device != DEVICE_HEADPHONE))
        {
            continue; // no jack, or a digital or input device
        }

        path_t *path = &paths[path_count];
        path->length = 0;
        path->device = (uint8_t)device;
        if (find_dac(n, path))
        {
            path_count++;
        }
    }
}

/**
 * the first codec with an analog output path.
 */
static bool find_codec(void)
{
    uint16_t present = rd16(STATESTS);
    for (codec = 0; codec < 15; codec++)
    {
        if (!(present & (1u << codec)))
        {
            continue;
        }
        uint32_t count = param(0, PARAM_NODE_COUNT);
        uint32_t first = (count >> 16) & 0xFFu;
        uint32_t total = count & 0xFFu;
        for (uint32_t n = first; n < first + total && n < MAX_NODES; n++)
        {
            if ((param(n, PARAM_FG_TYPE) & 0xFFu) == FG_AUDIO)
            {
                path_count = 0;
                scan_function_group(n);
                if (path_count > 0)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

/* ---- path setup ---------------------------------------------------------- */

static uint32_t out_amp_caps(uint32_t node)
{
    if (widget_caps[node] & CAPS_AMP_OVERRIDE)
    {
        return param(node, PARAM_OUT_AMP_CAPS);
    }
    return afg_out_amp;
}

static uint32_t in_amp_caps(uint32_t node)
{
    if (widget_caps[node] & CAPS_AMP_OVERRIDE)
    {
        return param(node, PARAM_IN_AMP_CAPS);
    }
    return afg_in_amp;
}

/**
 * the gain for a volume: steps * volume / 100 (the 0 dB step is the
 * caps' offset; the full range is used, so 100 is the loudest).
 */
static uint32_t gain_for(uint32_t caps, uint32_t volume)
{
    uint32_t steps = (caps >> 8) & 0x7Fu;
    return steps * volume / 100;
}

/**
 * set the output amp of the DAC (or of the pin, when the DAC has none)
 * of each path.
 */
static void set_path_volume(uint32_t left, uint32_t right)
{
    for (uint32_t p = 0; p < path_count; p++)
    {
        path_t *path = &paths[p];
        uint32_t node = path->nodes[path->length - 1];
        if (!(widget_caps[node] & CAPS_OUT_AMP))
        {
            node = path->nodes[0];
        }
        if (!(widget_caps[node] & CAPS_OUT_AMP))
        {
            continue;
        }
        uint32_t caps = out_amp_caps(node);
        uint32_t l = left ? gain_for(caps, left) : AMP_MUTE;
        uint32_t r = right ? gain_for(caps, right) : AMP_MUTE;
        verb4(node, VERB4_SET_AMP, AMP_OUT | 0x2000u | l); // left
        verb4(node, VERB4_SET_AMP, AMP_OUT | 0x1000u | r); // right
    }
}

static void setup_path(const path_t *path)
{
    for (uint32_t i = 0; i < path->length; i++)
    {
        uint32_t node = path->nodes[i];
        verb12(node, VERB_SET_POWER, 0);

        // the connection to the next node on the path
        for (uint32_t c = 0; i + 1 < path->length && c < conn_count[node]; c++)
        {
            if (conns[node][c] != path->nodes[i + 1])
            {
                continue;
            }
            if (WIDGET_TYPE(widget_caps[node]) == WIDGET_MIXER)
            {
                // a mixer takes all its inputs; unmute this one
                if (widget_caps[node] & CAPS_IN_AMP)
                {
                    uint32_t gain = in_amp_caps(node) & 0x7Fu;
                    verb4(node, VERB4_SET_AMP, AMP_IN | AMP_LEFT_RIGHT | (c << 8) | gain);
                }
            }
            else if (conn_count[node] > 1)
            {
                verb12(node, VERB_SET_SELECT, c);
            }
            break;
        }

        // unmute the output amp at 0 dB (the volume sets the DAC later)
        if (widget_caps[node] & CAPS_OUT_AMP)
        {
            uint32_t gain = out_amp_caps(node) & 0x7Fu;
            verb4(node, VERB4_SET_AMP, AMP_OUT | AMP_LEFT_RIGHT | gain);
        }
    }

    uint32_t pin = path->nodes[0];
    uint32_t pin_caps = param(pin, PARAM_PIN_CAPS);
    uint32_t control = PIN_CTL_OUT | (path->device == DEVICE_HEADPHONE ? PIN_CTL_HP : 0);
    verb12(pin, VERB_SET_PIN_CTL, control);
    if (pin_caps & PIN_CAPS_EAPD)
    {
        verb12(pin, VERB_SET_EAPD, EAPD_ON); // the external amplifier
    }

    uint32_t dac = path->nodes[path->length - 1];
    verb12(dac, VERB_SET_STREAM, STREAM_TAG << 4);
}

/* ---- rates --------------------------------------------------------------- */

static const struct
{
    uint32_t rate;
    uint16_t format; // base, multiplier and divisor bits of the stream format
    uint32_t pcm_bit; // the bit in PARAM_PCM
} rates[] = {
    {8000, 0x0500, 1u << 0},  {11025, 0x4300, 1u << 1}, {16000, 0x0200, 1u << 2},
    {22050, 0x4100, 1u << 3}, {32000, 0x0A00, 1u << 4}, {44100, 0x4000, 1u << 5},
    {48000, 0x0000, 1u << 6},
};

#define FORMAT_16BIT_STEREO 0x0011u
#define RATE_COUNT (sizeof(rates) / sizeof(rates[0]))

static uint32_t supported_rates;

static uint32_t hda_set_rate(audio_device_t *dev, uint32_t rate)
{
    (void)dev;
    uint32_t best = RATE_COUNT - 1; // 48 kHz, which every codec has
    uint32_t best_diff = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < RATE_COUNT; i++)
    {
        if (!(supported_rates & rates[i].pcm_bit))
        {
            continue;
        }
        uint32_t diff = rates[i].rate > rate ? rates[i].rate - rate : rate - rates[i].rate;
        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }
    }
    stream_format = rates[best].format | FORMAT_16BIT_STEREO;
    return rates[best].rate;
}

/* ---- playback ------------------------------------------------------------ */

static uint8_t *period(uint32_t index)
{
    return dma + DMA_BUFFERS + index * PERIOD_BYTES;
}

/**
 * fill the periods the stream has played since the last refill.
 */
static void refill(void)
{
    if (!playing)
    {
        return;
    }
    uint32_t current = (mmio_read32(sd + SD_LPIB) / PERIOD_BYTES) % PERIODS;
    for (uint32_t n = 0; fill_period != current && n < PERIODS; n++)
    {
        audio_pull(&audio, period(fill_period), PERIOD_BYTES);
        fill_period = (fill_period + 1) % PERIODS;
    }
}

static void hda_interrupt(void *context)
{
    (void)context;
    if (!(rd32(INTSTS) & (1u << stream_index)))
    {
        return; // the shared line was raised by another device
    }
    mmio_write8(sd + SD_STS, SD_STS_CLEAR);
    refill();
}

static void hda_timer(uint64_t now)
{
    (void)now;
    refill();
}

static void stream_reset(void)
{
    mmio_write8(sd + SD_CTL0, 0);
    for (uint32_t spin = 0; (mmio_read8(sd + SD_CTL0) & SD_RUN) && spin < SPIN_LIMIT; spin++)
    {
    }
    mmio_write8(sd + SD_CTL0, SD_SRST);
    for (uint32_t spin = 0; !(mmio_read8(sd + SD_CTL0) & SD_SRST) && spin < SPIN_LIMIT; spin++)
    {
    }
    mmio_write8(sd + SD_CTL0, 0);
    for (uint32_t spin = 0; (mmio_read8(sd + SD_CTL0) & SD_SRST) && spin < SPIN_LIMIT; spin++)
    {
    }
}

static bool hda_start(audio_device_t *dev)
{
    (void)dev;
    stream_reset();

    bdl_entry_t *bdl = (bdl_entry_t *)(dma + DMA_BDL);
    for (uint32_t i = 0; i < PERIODS; i++)
    {
        bdl[i].address = dma_phys + DMA_BUFFERS + i * PERIOD_BYTES;
        bdl[i].length = PERIOD_BYTES;
        bdl[i].flags = 1; // interrupt on completion
        audio_pull(&audio, period(i), PERIOD_BYTES);
    }
    fill_period = 0;

    phys_addr_t bdl_phys = dma_phys + DMA_BDL;
    mmio_write32(sd + SD_BDPL, (uint32_t)bdl_phys);
    mmio_write32(sd + SD_BDPU, (uint32_t)((uint64_t)bdl_phys >> 32));
    mmio_write32(sd + SD_CBL, PERIODS * PERIOD_BYTES);
    mmio_write16(sd + SD_LVI, PERIODS - 1);
    mmio_write16(sd + SD_FMT, stream_format);
    mmio_write8(sd + SD_CTL2, (uint8_t)(STREAM_TAG << 4));
    mmio_write8(sd + SD_STS, SD_STS_CLEAR);

    for (uint32_t p = 0; p < path_count; p++)
    {
        verb4(paths[p].nodes[paths[p].length - 1], VERB4_SET_FORMAT, stream_format);
    }

    playing = true;
    wr32(INTCTL, rd32(INTCTL) | INTCTL_GIE | (1u << stream_index));
    mmio_write8(sd + SD_CTL0, SD_RUN | SD_IOCE);
    return true;
}

static void hda_stop(audio_device_t *dev)
{
    (void)dev;
    playing = false;
    stream_reset();
}

static void hda_set_volume(audio_device_t *dev, uint32_t left, uint32_t right)
{
    (void)dev;
    set_path_volume(left, right);
}

static const audio_ops_t hda_ops = {
    .start = hda_start,
    .stop = hda_stop,
    .set_rate = hda_set_rate,
    .set_volume = hda_set_volume,
};

/* ---- init ---------------------------------------------------------------- */

/**
 * a short name for the outputs in use, for /proc/audio.
 */
static const char *describe_outputs(void)
{
    bool line = false;
    bool speaker = false;
    bool headphone = false;
    for (uint32_t p = 0; p < path_count; p++)
    {
        line |= paths[p].device == DEVICE_LINE_OUT;
        speaker |= paths[p].device == DEVICE_SPEAKER;
        headphone |= paths[p].device == DEVICE_HEADPHONE;
    }
    if (speaker && headphone)
    {
        return "speaker and headphones";
    }
    if (speaker)
    {
        return "speaker";
    }
    if (headphone)
    {
        return line ? "line out and headphones" : "headphones";
    }
    return "line out";
}

bool hda_init(void)
{
    pci_device_t *dev = pci_find_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HDA, -1, 0);
    if (!dev || dev->bars[0].base == 0 || dev->bars[0].is_io ||
        (sizeof(phys_addr_t) < 8 && (dev->bars[0].base >> 32) != 0))
    {
        return false;
    }

    regs = ioremap_nocache((phys_addr_t)dev->bars[0].base, 0x4000);
    dma = pmm_alloc_contiguous(DMA_PAGES);
    if (!regs || !dma)
    {
        return false;
    }
    dma_phys = virt_to_phys(dma);
    bool dma_64 = (rd16(GCAP) & GCAP_64OK) != 0;
    if (!dma_64 && (uint64_t)dma_phys + DMA_PAGES * PAGE_SIZE > 0x100000000ull)
    {
        KLOG_WARN("hda: no DMA memory below 4 GiB");
        return false;
    }
    memset(dma, 0, DMA_PAGES * PAGE_SIZE);
    corb = (volatile uint32_t *)(dma + DMA_CORB);
    rirb = (volatile uint32_t *)(dma + DMA_RIRB);
    pci_enable_bus_master(dev);

    if (!reset_controller() || !setup_rings())
    {
        KLOG_WARN("hda: the controller did not reset");
        return false;
    }
    if (!find_codec())
    {
        KLOG_WARN("hda: no codec with an analog output");
        return false;
    }

    // the first output stream comes after the input streams
    uint16_t gcap = rd16(GCAP);
    uint32_t inputs = (gcap >> 8) & 0xFu;
    uint32_t outputs = (gcap >> 12) & 0xFu;
    if (outputs == 0)
    {
        KLOG_WARN("hda: the controller has no output stream");
        return false;
    }
    stream_index = inputs;
    sd = regs + SD_BASE + stream_index * SD_SIZE;

    for (uint32_t p = 0; p < path_count; p++)
    {
        setup_path(&paths[p]);
    }

    // the rates the first DAC takes (its own list, else the group's)
    uint32_t dac = paths[0].nodes[paths[0].length - 1];
    supported_rates = param(dac, PARAM_PCM);
    if ((supported_rates & 0xFFFu) == 0)
    {
        supported_rates = param(afg, PARAM_PCM);
    }
    supported_rates |= rates[RATE_COUNT - 1].pcm_bit; // 48 kHz is required

    audio.name = "hda";
    audio.ops = &hda_ops;
    audio.rate = hda_set_rate(&audio, 48000);
    audio.period_bytes = PERIOD_BYTES;
    audio.periods = PERIODS;
    audio.codec_id = param(0, PARAM_VENDOR);
    audio.detail = describe_outputs();

    pci_irq_attach(dev, hda_interrupt, NULL, "hda");
    timer_add_poll_hook(hda_timer);
    KLOG_INFO("hda: codec %u (%08x), %u output path(s): %s", codec, audio.codec_id, path_count,
              audio.detail);
    return audio_register(&audio);
}
