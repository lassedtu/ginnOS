/**
 * the AC'97 driver (Intel I/O Controller Hub 82801AA AC'97 programmer's
 * reference; AC'97 component specification 2.3).
 *
 * the PCM out channel reads a buffer descriptor list (BDL) of 32 entries;
 * each entry points to a buffer and gives its length in samples. the
 * controller plays the entries in a circle and stops after the last valid
 * index (LVI). the driver uses PERIODS buffers: entry e plays buffer
 * e % PERIODS. when an entry has played, the driver fills its buffer
 * again from the audio core and moves the LVI forward, so the controller
 * never reaches it while sound is queued.
 *
 * the refill runs from the controller interrupt and from the timer (the
 * controller has no MSI, and its INTx line may not be routed on a real
 * machine with the I/O APIC). both run with interrupts off, so they never
 * overlap.
 */

#include "ac97.h"

#include "arch/x86/cpu/io.h"
#include "common/memory.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_irq.h"
#include "kernel/audio/audio.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"
#include "kernel/time/timer.h"

// mixer registers (NAM)
#define NAM_RESET        0x00
#define NAM_MASTER       0x02
#define NAM_PCM_OUT      0x18
#define NAM_EXT_ID       0x28
#define NAM_EXT_CTRL     0x2A
#define NAM_FRONT_RATE   0x2C
#define NAM_VENDOR_ID1   0x7C
#define NAM_VENDOR_ID2   0x7E

#define EXT_VRA          0x0001u // variable rate audio
#define VOLUME_MUTE      0x8000u
#define PCM_OUT_0DB      0x0808u

// bus master registers (NABM): the PCM out box, and the global ones
#define PO_BDBAR  0x10
#define PO_CIV    0x14
#define PO_LVI    0x15
#define PO_SR     0x16
#define PO_CR     0x1B
#define GLOB_CNT  0x2C
#define GLOB_STA  0x30

#define CR_RUN    0x01u // run bus master
#define CR_RESET  0x02u // reset the box registers
#define CR_IOCE   0x10u // interrupt on completion
#define SR_DCH    0x01u // halted
#define SR_LVBCI  0x04u // last valid buffer done
#define SR_BCIS   0x08u // a buffer with IOC done
#define SR_FIFOE  0x10u // FIFO error
#define SR_CLEAR  (SR_LVBCI | SR_BCIS | SR_FIFOE)

#define GLOB_COLD_RESET_OFF 0x02u // 1 = out of cold reset
#define GLOB_CODEC_READY    0x100u

#define BDL_ENTRIES   32u
#define BDL_IOC       0x8000u
#define BDL_BUP       0x4000u // after the last buffer, play silence

#define PERIODS       4u
#define PERIOD_BYTES  2048u // 512 frames
#define SPIN_LIMIT    10000000u

typedef struct
{
    uint32_t address;
    uint16_t samples;
    uint16_t flags;
} __attribute__((packed)) bdl_entry_t;

static uint16_t nam;
static uint16_t nabm;
static bool variable_rate;
static uint8_t *dma;          // the BDL, then the period buffers
static phys_addr_t dma_phys;
static bdl_entry_t *bdl;
static volatile bool playing;
static uint32_t fill_index;   // the next BDL entry to fill (counts up, mod 32 when used)
static audio_device_t audio;

#define BUFFERS_OFFSET 0x400u // after the BDL (256 bytes), aligned

static uint8_t *buffer(uint32_t entry)
{
    return dma + BUFFERS_OFFSET + (entry % PERIODS) * PERIOD_BYTES;
}

static phys_addr_t buffer_phys(uint32_t entry)
{
    return dma_phys + BUFFERS_OFFSET + (entry % PERIODS) * PERIOD_BYTES;
}

/**
 * fill the buffers of the entries that have played, up to PERIODS entries
 * ahead of the one that plays now, and move the LVI to the last filled.
 */
static void refill(void)
{
    if (!playing)
    {
        return;
    }

    uint32_t civ = io_inb(nabm + PO_CIV) % BDL_ENTRIES;

    // fill_index runs ahead of civ by at most PERIODS entries; bring both
    // into one window of 32 entries
    uint32_t ahead = (fill_index - civ) % BDL_ENTRIES;
    uint32_t filled = 0;
    while (ahead < PERIODS && filled < PERIODS)
    {
        audio_pull(&audio, buffer(fill_index), PERIOD_BYTES);
        fill_index = (fill_index + 1) % BDL_ENTRIES;
        ahead = (fill_index - civ) % BDL_ENTRIES;
        filled++;
    }
    if (filled > 0)
    {
        io_outb(nabm + PO_LVI, (uint8_t)((fill_index + BDL_ENTRIES - 1) % BDL_ENTRIES));
    }

    // halted (the interrupt and the timer were both late): start again
    if (io_inw(nabm + PO_SR) & SR_DCH)
    {
        io_outb(nabm + PO_CR, CR_RUN | CR_IOCE);
    }
}

static void ac97_interrupt(void *context)
{
    (void)context;
    uint16_t status = io_inw(nabm + PO_SR);
    if (!(status & SR_CLEAR))
    {
        return; // the shared line was raised by another device
    }
    io_outw(nabm + PO_SR, status & SR_CLEAR);
    refill();
}

static void ac97_timer(uint64_t now)
{
    (void)now;
    refill();
}

static void reset_box(void)
{
    io_outb(nabm + PO_CR, 0);
    for (uint32_t spin = 0; !(io_inw(nabm + PO_SR) & SR_DCH) && spin < SPIN_LIMIT; spin++)
    {
    }
    io_outb(nabm + PO_CR, CR_RESET);
    for (uint32_t spin = 0; (io_inb(nabm + PO_CR) & CR_RESET) && spin < SPIN_LIMIT; spin++)
    {
    }
}

static bool ac97_start(audio_device_t *dev)
{
    (void)dev;
    reset_box();
    io_outl(nabm + PO_BDBAR, (uint32_t)dma_phys);

    for (uint32_t e = 0; e < BDL_ENTRIES; e++)
    {
        bdl[e].address = (uint32_t)buffer_phys(e);
        bdl[e].samples = PERIOD_BYTES / 2; // 16-bit samples, both channels
        bdl[e].flags = BDL_IOC | BDL_BUP;
    }
    for (uint32_t e = 0; e < PERIODS; e++)
    {
        audio_pull(&audio, buffer(e), PERIOD_BYTES);
    }
    fill_index = PERIODS;
    io_outb(nabm + PO_LVI, PERIODS - 1);
    io_outw(nabm + PO_SR, SR_CLEAR);

    playing = true;
    io_outb(nabm + PO_CR, CR_RUN | CR_IOCE);
    return true;
}

static void ac97_stop(audio_device_t *dev)
{
    (void)dev;
    playing = false;
    reset_box();
}

static uint32_t ac97_set_rate(audio_device_t *dev, uint32_t rate)
{
    (void)dev;
    if (!variable_rate)
    {
        return 48000;
    }
    if (rate < 8000)
    {
        rate = 8000;
    }
    if (rate > 48000)
    {
        rate = 48000;
    }
    io_outw(nam + NAM_FRONT_RATE, (uint16_t)rate);
    return io_inw(nam + NAM_FRONT_RATE); // the codec may round it
}

static void ac97_set_volume(audio_device_t *dev, uint32_t left, uint32_t right)
{
    (void)dev;
    if (left == 0 && right == 0)
    {
        io_outw(nam + NAM_MASTER, VOLUME_MUTE);
        return;
    }
    // 5 bits of attenuation in 1.5 dB steps (0 is the loudest)
    uint16_t att_left = (uint16_t)((100 - left) * 31 / 100);
    uint16_t att_right = (uint16_t)((100 - right) * 31 / 100);
    io_outw(nam + NAM_MASTER, (uint16_t)((att_left << 8) | att_right));
}

static const audio_ops_t ac97_ops = {
    .start = ac97_start,
    .stop = ac97_stop,
    .set_rate = ac97_set_rate,
    .set_volume = ac97_set_volume,
};

/**
 * take the codec out of cold reset and wait for it.
 */
static bool codec_reset(void)
{
    io_outl(nabm + GLOB_CNT, GLOB_COLD_RESET_OFF);
    for (uint32_t spin = 0; spin < SPIN_LIMIT; spin++)
    {
        if (io_inl(nabm + GLOB_STA) & GLOB_CODEC_READY)
        {
            io_outw(nam + NAM_RESET, 0); // the mixer to its defaults
            return true;
        }
    }
    return false;
}

bool ac97_init(void)
{
    pci_device_t *dev = pci_find_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AC97, -1, 0);
    if (!dev || !dev->bars[0].is_io || !dev->bars[1].is_io)
    {
        return false;
    }
    nam = (uint16_t)dev->bars[0].base;
    nabm = (uint16_t)dev->bars[1].base;

    // I/O ports and bus mastering on
    pci_write16(dev, PCI_COMMAND,
                pci_read16(dev, PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);

    if (!codec_reset())
    {
        KLOG_WARN("ac97: the codec is not ready");
        return false;
    }

    // the BDL and the buffers: one block, 32-bit addresses
    uint32_t pages = (BUFFERS_OFFSET + PERIODS * PERIOD_BYTES + PAGE_SIZE - 1) / PAGE_SIZE;
    dma = pmm_alloc_contiguous(pages);
    if (!dma)
    {
        return false;
    }
    dma_phys = virt_to_phys(dma);
    if ((uint64_t)dma_phys + pages * PAGE_SIZE > 0x100000000ull)
    {
        KLOG_WARN("ac97: no DMA memory below 4 GiB");
        return false;
    }
    memset(dma, 0, pages * PAGE_SIZE);
    bdl = (bdl_entry_t *)dma;

    variable_rate = (io_inw(nam + NAM_EXT_ID) & EXT_VRA) != 0;
    if (variable_rate)
    {
        io_outw(nam + NAM_EXT_CTRL, io_inw(nam + NAM_EXT_CTRL) | EXT_VRA);
    }
    io_outw(nam + NAM_PCM_OUT, PCM_OUT_0DB);
    reset_box();

    audio.name = "ac97";
    audio.ops = &ac97_ops;
    audio.rate = ac97_set_rate(&audio, 48000);
    audio.period_bytes = PERIOD_BYTES;
    audio.periods = PERIODS;
    audio.codec_id = ((uint32_t)io_inw(nam + NAM_VENDOR_ID1) << 16) | io_inw(nam + NAM_VENDOR_ID2);
    audio.detail = variable_rate ? "variable rate" : "fixed rate 48 kHz";

    pci_irq_attach(dev, ac97_interrupt, NULL, "ac97");
    timer_add_poll_hook(ac97_timer);
    return audio_register(&audio);
}
