/**
 * the local APIC and the I/O APIC (Intel SDM volume 3, chapter 11; the
 * 82093AA I/O APIC datasheet).
 *
 * the local APIC registers are 32 bits wide, 16 bytes apart, at the
 * address from the MADT. the I/O APIC has two registers: IOREGSEL selects
 * a register, IOWIN reads or writes it. its redirection table has one
 * 64-bit entry per global system interrupt (GSI).
 *
 * uniprocessor: the kernel uses the local APIC of the CPU it boots on.
 */

#include "apic.h"

#include "idt.h"
#include "io.h"
#include "isr.h"
#include "pic.h"
#include "irq.h"
#include "kernel/acpi/acpi.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"

// local APIC registers
#define LAPIC_ID          0x020
#define LAPIC_TPR         0x080
#define LAPIC_EOI         0x0B0
#define LAPIC_SVR         0x0F0
#define LAPIC_LVT_TIMER   0x320
#define LAPIC_LVT_LINT0   0x350
#define LAPIC_LVT_LINT1   0x360
#define LAPIC_LVT_ERROR   0x370
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_COUNT 0x390
#define LAPIC_TIMER_DIV   0x3E0

#define LAPIC_SVR_ENABLE  0x100u
#define LVT_MASKED        0x10000u
#define LVT_TIMER_PERIODIC 0x20000u
#define LVT_DELIVERY_EXTINT 0x700u
#define LVT_DELIVERY_NMI  0x400u
#define TIMER_DIVIDE_16   0x3u

#define SPURIOUS_VECTOR 0xFF

#define MSR_APIC_BASE        0x1Bu
#define APIC_BASE_ENABLE     (1u << 11)
#define APIC_DEFAULT_ADDRESS 0xFEE00000u

// I/O APIC registers
#define IOAPIC_REGSEL  0x00
#define IOAPIC_WIN     0x10
#define IOAPIC_VERSION 0x01
#define IOAPIC_REDTBL  0x10

#define REDIR_ACTIVE_LOW 0x2000u
#define REDIR_LEVEL      0x8000u
#define REDIR_MASKED     0x10000u

// the PIT input clock, and 10 ms of it for the timer calibration
#define PIT_CLOCK_HZ   1193182u
#define CALIBRATE_MS   10u

static volatile uint32_t *lapic;
static bool active;
static bool timer_on_lapic; // IRQ 0 comes from the local APIC timer

typedef struct
{
    volatile uint32_t *regs;
    uint32_t gsi_base;
    uint32_t entries;
} ioapic_t;

static ioapic_t ioapics[ACPI_MAX_IOAPICS];
static uint32_t ioapic_count;

// the I/O APIC entry of each ISA IRQ (filled by apic_init)
static struct
{
    ioapic_t *ioapic;
    uint32_t index;
} isa_routes[16];

static uint32_t lapic_read(uint32_t reg)
{
    return mmio_read32(&lapic[reg / 4]);
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    mmio_write32(&lapic[reg / 4], value);
}

static uint32_t ioapic_read(const ioapic_t *ioapic, uint32_t reg)
{
    mmio_write32(&ioapic->regs[IOAPIC_REGSEL / 4], reg);
    return mmio_read32(&ioapic->regs[IOAPIC_WIN / 4]);
}

static void ioapic_write(const ioapic_t *ioapic, uint32_t reg, uint32_t value)
{
    mmio_write32(&ioapic->regs[IOAPIC_REGSEL / 4], reg);
    mmio_write32(&ioapic->regs[IOAPIC_WIN / 4], value);
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static bool cpu_has_apic(void)
{
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx = 0;
    uint32_t edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (edx & (1u << 9)) != 0;
}

/**
 * the spurious vector: the local APIC sends it for an interrupt that went
 * away. it must not get an EOI.
 */
static void spurious_handler(struct registers *regs)
{
    (void)regs;
}

/**
 * turn the local APIC on (the bootloader can leave it off) and map it.
 */
static bool lapic_enable(uint32_t address)
{
    uint64_t base = read_msr(MSR_APIC_BASE);
    if (!(base & APIC_BASE_ENABLE))
    {
        write_msr(MSR_APIC_BASE, base | APIC_BASE_ENABLE);
    }

    lapic = ioremap_nocache(address, 0x1000);
    if (!lapic)
    {
        return false;
    }

    lapic_write(LAPIC_TPR, 0); // accept all interrupt priorities
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | SPURIOUS_VECTOR);
    return true;
}

/**
 * count local APIC timer ticks (divide by 16) over 10 ms of PIT channel 2,
 * which runs without interrupts (one-shot, its output read at port 0x61).
 * @return the ticks per second, or 0 if the count is not usable.
 */
static uint32_t calibrate_timer(void)
{
    uint32_t pit_count = PIT_CLOCK_HZ / (1000u / CALIBRATE_MS);

    // gate on, speaker off
    uint8_t port61 = io_inb(0x61);
    io_outb(0x61, (uint8_t)((port61 & ~0x02u) | 0x01u));

    io_outb(0x43, 0xB0); // channel 2, low then high byte, mode 0
    io_outb(0x42, (uint8_t)(pit_count & 0xFF));
    io_outb(0x42, (uint8_t)(pit_count >> 8));

    // a low-high edge on the gate starts the count
    port61 = io_inb(0x61);
    io_outb(0x61, (uint8_t)(port61 & ~0x01u));
    io_outb(0x61, (uint8_t)(port61 | 0x01u));

    lapic_write(LAPIC_TIMER_DIV, TIMER_DIVIDE_16);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFFu);

    // OUT2 (bit 5) goes high when the count ends; limit the wait
    for (uint32_t spin = 0; !(io_inb(0x61) & 0x20); spin++)
    {
        if (spin > 100000000u)
        {
            return 0;
        }
    }

    uint32_t elapsed = 0xFFFFFFFFu - lapic_read(LAPIC_TIMER_COUNT);
    lapic_write(LAPIC_TIMER_INIT, 0);
    io_outb(0x61, port61 & ~0x03u);

    return elapsed * (1000u / CALIBRATE_MS);
}

/**
 * start the periodic local APIC timer on the vector of IRQ 0.
 */
static bool start_timer(uint32_t timer_hz)
{
    uint32_t per_second = calibrate_timer();
    if (per_second < timer_hz * 100u)
    {
        KLOG_WARN("apic: timer calibration failed; the PIT stays the tick");
        return false;
    }

    lapic_write(LAPIC_TIMER_DIV, TIMER_DIVIDE_16);
    lapic_write(LAPIC_LVT_TIMER, LVT_TIMER_PERIODIC | (PIC_REMAP_OFFSET + 0));
    lapic_write(LAPIC_TIMER_INIT, per_second / timer_hz);
    KLOG_INFO("apic: timer %u ticks per second (divide 16), %u Hz", per_second, timer_hz);
    return true;
}

/**
 * set up the redirection entries of the ISA IRQs, all masked: the IRQ
 * layer unmasks the ones that have handlers (apic_set_irq_masked).
 */
static void route_isa_irqs(uint8_t lapic_id)
{
    for (uint8_t irq = 0; irq < 16; irq++)
    {
        bool active_low;
        bool level;
        uint32_t gsi = acpi_irq_to_gsi(irq, &active_low, &level);

        for (uint32_t i = 0; i < ioapic_count; i++)
        {
            ioapic_t *ioapic = &ioapics[i];
            if (gsi < ioapic->gsi_base || gsi >= ioapic->gsi_base + ioapic->entries)
            {
                continue;
            }

            uint32_t index = gsi - ioapic->gsi_base;
            uint32_t low = (PIC_REMAP_OFFSET + irq) | REDIR_MASKED;
            if (active_low)
            {
                low |= REDIR_ACTIVE_LOW;
            }
            if (level)
            {
                low |= REDIR_LEVEL;
            }
            ioapic_write(ioapic, IOAPIC_REDTBL + index * 2 + 1, (uint32_t)lapic_id << 24);
            ioapic_write(ioapic, IOAPIC_REDTBL + index * 2, low);

            isa_routes[irq].ioapic = ioapic;
            isa_routes[irq].index = index;
            break;
        }
    }
}

/**
 * no MADT: keep the 8259s. if the bootloader left the local APIC on with
 * LINT0 masked (limine does), the 8259 interrupts would not arrive, so put
 * LINT0 in virtual wire mode (ExtINT) and LINT1 as NMI.
 */
static void keep_8259(void)
{
    if (!cpu_has_apic() || !(read_msr(MSR_APIC_BASE) & APIC_BASE_ENABLE))
    {
        return;
    }

    uint32_t address = (uint32_t)(read_msr(MSR_APIC_BASE) & 0xFFFFF000u);
    lapic = ioremap_nocache(address, 0x1000);
    if (!lapic)
    {
        return;
    }
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | SPURIOUS_VECTOR);
    lapic_write(LAPIC_LVT_LINT0, LVT_DELIVERY_EXTINT);
    lapic_write(LAPIC_LVT_LINT1, LVT_DELIVERY_NMI);
    lapic = NULL;
}

bool apic_init(uint32_t timer_hz)
{
    isr_register_handler(SPURIOUS_VECTOR, spurious_handler);

    const acpi_info_t *acpi = acpi_info();
    if (!cpu_has_apic() || !acpi->has_madt || acpi->ioapic_count == 0)
    {
        keep_8259();
        KLOG_INFO("apic: no MADT or I/O APIC; using the 8259 PICs");
        return false;
    }

    for (uint32_t i = 0; i < acpi->ioapic_count; i++)
    {
        ioapic_t *ioapic = &ioapics[ioapic_count];
        ioapic->regs = ioremap_nocache(acpi->ioapics[i].address, 0x20);
        if (!ioapic->regs)
        {
            continue;
        }
        ioapic->gsi_base = acpi->ioapics[i].gsi_base;
        ioapic->entries = ((ioapic_read(ioapic, IOAPIC_VERSION) >> 16) & 0xFFu) + 1;
        ioapic_count++;
    }

    uint32_t address = acpi->lapic_address ? acpi->lapic_address : APIC_DEFAULT_ADDRESS;
    if (ioapic_count == 0 || !lapic_enable(address))
    {
        keep_8259();
        return false;
    }

    // the 8259s stay remapped at 32 to 47, so a spurious interrupt from them
    // lands on an IRQ vector, but all their lines are masked
    for (int irq = 0; irq < 16; irq++)
    {
        pic_mask(irq);
    }

    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);

    uint8_t lapic_id = (uint8_t)(lapic_read(LAPIC_ID) >> 24);
    route_isa_irqs(lapic_id);
    active = true;

    timer_on_lapic = start_timer(timer_hz);

    // the lines the IRQ layer enabled under the 8259 are enabled again here
    for (uint32_t irq = 0; irq < 16; irq++)
    {
        if (irq_line_wanted(irq))
        {
            apic_set_irq_masked(irq, false);
        }
    }

    KLOG_INFO("apic: local APIC %u, %u I/O APIC(s), 8259 masked", lapic_id, ioapic_count);
    return true;
}

bool apic_active(void)
{
    return active;
}

void apic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

void apic_set_irq_masked(uint32_t irq, bool masked)
{
    if (irq >= 16 || !isa_routes[irq].ioapic)
    {
        return;
    }

    // the tick comes from the local APIC timer: the PIT line stays masked
    if (irq == 0 && timer_on_lapic)
    {
        masked = true;
    }

    ioapic_t *ioapic = isa_routes[irq].ioapic;
    uint32_t reg = IOAPIC_REDTBL + isa_routes[irq].index * 2;
    uint32_t low = ioapic_read(ioapic, reg);
    low = masked ? (low | REDIR_MASKED) : (low & ~REDIR_MASKED);
    ioapic_write(ioapic, reg, low);
}
