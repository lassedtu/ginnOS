# The drivers

The drivers are in the kernel. Each one registers its devices in the
device registry, and devfs shows them in `/dev` (see
[The file systems](filesystems.md#devfs)).

The code: [`src/drivers/`](https://github.com/lassedtu/ginnOS/tree/main/src/drivers),
[`src/kernel/acpi/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/acpi),
[`src/arch/x86/cpu/apic.c`](https://github.com/lassedtu/ginnOS/blob/main/src/arch/x86/cpu/apic.c)

## The list

| Kind | Driver | Hardware | Code |
| --- | --- | --- | --- |
| disk | ATA | IDE disks (PIO) | [`disk/ata.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/disk/ata.c) |
| disk | AHCI | SATA disks, with DMA and 48-bit LBA | [`disk/ahci.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/disk/ahci.c) |
| disk | NVMe | the first NVMe controller, namespace 1, 512-byte sectors | [`disk/nvme.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/disk/nvme.c) |
| disk | ramdisk | the root image that Limine loads. Writes go at reboot. | [`disk/ramdisk.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/disk/ramdisk.c) |
| disk | partitions | finds the ext2 partition in an MBR or a GPT table, and makes it a block device (`hda1`) | [`disk/partition.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/disk/partition.c) |
| keyboard | PS/2 keyboard | the i8042 controller, with the layouts `dk-mac` and `us` | [`keyboard/`](https://github.com/lassedtu/ginnOS/tree/main/src/drivers/keyboard) |
| pointer | PS/2 mouse | the i8042 aux port, with a wheel | [`mouse/ps2_mouse.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/mouse/ps2_mouse.c) |
| USB | xHCI and HID | USB keyboards, mice and tablets (QEMU's tablet) on xHCI controllers | [`usb/`](https://github.com/lassedtu/ginnOS/tree/main/src/drivers/usb) |
| sound | HDA | Intel High Definition Audio: line out, speakers, headphones | [`sound/hda.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/sound/hda.c) |
| sound | AC'97 | Intel ICH AC'97, if there is no HDA | [`sound/ac97.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/sound/ac97.c) |
| video | framebuffer | the framebuffer from the bootloader | [`video/fb/`](https://github.com/lassedtu/ginnOS/tree/main/src/drivers/video/fb) |
| video | virtio-gpu | QEMU `-vga virtio`: modes at run time | [`video/virtio_gpu.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/video/virtio_gpu.c) |
| network | virtio-net | QEMU and most virtual machines | [`net/virtio_net.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/net/virtio_net.c) |
| bus | PCI | finds all PCI functions through ports `0xCF8` and `0xCFC` | [`pci/`](https://github.com/lassedtu/ginnOS/tree/main/src/drivers/pci) |
| bus | virtio | virtio 1.0 on PCI, for virtio-gpu and virtio-net | [`virtio/virtio.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/virtio/virtio.c) |
| time | PIT | the 8254 timer, before the APIC timer starts | [`pit/pit.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/pit/pit.c) |
| time | RTC | the CMOS clock: the date and time at boot | [`rtc/rtc.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/rtc/rtc.c) |
| debug | serial | COM1 (16550), output only, for the kernel log | [`serial/serial.c`](https://github.com/lassedtu/ginnOS/blob/main/src/drivers/serial/serial.c) |

## Interrupts

The kernel starts with the 8259 PICs. If the ACPI MADT table describes
the APICs, the kernel masks the 8259s and uses the I/O APIC for the ISA
interrupts and the local APIC timer for the scheduler tick.

A PCI driver attaches one handler to its device with `pci_irq_attach`.
It uses the best method that the device and the CPU have: MSI-X, then
MSI, then the shared INTx line. MSI and MSI-X need the local APIC.
`/proc/interrupts` shows each device, its method and its count.

## Wait for a device

A driver that waits for its device starts the command, then calls
`completion_wait_until`. The process sleeps until the interrupt, so
other processes run. The driver polls in place of a sleep when:

- the device has no interrupt;
- the kernel cannot sleep yet (during boot), or interrupts are off;
- the interrupt did not come. Then the driver polls from that time on,
  and writes this in the kernel log.

## ACPI

The kernel reads the static ACPI tables: RSDP, RSDT or XSDT, MADT,
FADT, HPET and MCFG. It has no AML interpreter, so it cannot find
devices through ACPI. To power off, it reads only the `_S5` package from
the DSDT. To restart, it uses the reset register of the FADT.

## What a real PC needs

The drivers work in QEMU. On a real PC, these parts are missing:

- network cards other than virtio-net (Intel, Realtek, Wi-Fi);
- graphics drivers: the screen stays in the mode that the firmware set;
- USB storage, USB hubs, and USB devices that are not HID;
- more than one CPU.

See [Get started](../use/getting-started.md) for what works on a real
PC.
