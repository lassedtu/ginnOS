# Get started

This page tells how to start ginnOS in QEMU and on a real PC, and how to
log in.

The code: [`mk/run.mk`](https://github.com/lassedtu/ginnOS/blob/main/mk/run.mk)
(QEMU), [`tools/limine/`](https://github.com/lassedtu/ginnOS/tree/main/tools/limine)
(the boot disk)

## Build the image

ginnOS does not have a download yet. Build the disk image from the
source: see [Set up the build tools](../develop/setup.md) and
[Build ginnOS](../develop/building.md). The short form is:

```sh
git clone https://github.com/lassedtu/ginnOS
cd ginnOS
make
```

## Start ginnOS in QEMU

```sh
make run
```

QEMU opens a window of 1440x900. The Limine boot menu waits 3 seconds,
then ginnOS starts and shows the login screen.

QEMU gives ginnOS these devices:

- a virtio-vga graphics card: when you change the size of the QEMU
  window, or set it to full screen, the desktop changes to the new size
- an Intel HDA sound card, which plays through the sound of your
  computer
- a virtio-net network card on the QEMU user network: ginnOS gets the
  address 10.0.2.15 and the gateway 10.0.2.2 from DHCP

To start the 64-bit system:

```sh
make ARCH=x86_64
make run ARCH=x86_64
```

To start with UEFI firmware in place of a BIOS:

```sh
make run-uefi
```

`make run-uefi` uses the OVMF firmware of QEMU. If `make` does not find
it, set `OVMF_CODE=` to the path of `OVMF_CODE.fd`.

For the options of `make run` (the window size, the sound card, no
network), see [the build options](../develop/building.md#the-build-options).

## Log in

The login screen shows the date, a large clock, the name of a user and
a Password field. It selects the user who logged in last.

1. To log in as a different user, click the name and select the user.
2. Type the password in the Password field.
3. Press Enter, or click Log in.

If the password is not correct, the screen shows "Incorrect password.
Try again." The Power off and Restart buttons are below the field.

| User | Password |
| --- | --- |
| `root` | `ginnos` |
| `user` | `user` |

Change the passwords if other people can use the machine. Use `passwd`
in the terminal.

To start the shell on the console in place of the login screen, set
`session = text` in `/etc/ginnos/boot.conf`. ginnOS also starts the
shell on the console when there is no display, or when the login screen
does not start.

## Start ginnOS on a real PC

Make sure that you have the correct device name before you write the
image. `dd` erases all of the USB stick.

1. Write the image to a USB stick:

   ```sh
   sudo dd if=build/ginnos.img of=/dev/<usb-stick> bs=4M conv=fsync
   ```

2. Start the PC from the USB stick. ginnOS boots with BIOS and with UEFI.

ginnOS cannot read USB storage. When it boots from a USB stick, its root
filesystem is the copy in RAM that Limine loads. Changes are lost when
the PC stops.

ginnOS uses a disk as its root only if the disk has an ext2 partition
with the label `GINNOS_EXT2`. It looks on the IDE disk, on SATA disks
and on the NVMe disk. It only reads the partition tables and the
superblocks of the other disks, so it does not write to the disk of
another operating system.

### What works on a real PC

- The boot with BIOS and with UEFI, through Limine.
- PS/2 and USB keyboards (USB through xHCI). There are also drivers for
  PS/2 and USB mice. USB hubs do not work yet.
- The screen that the firmware sets up. ginnOS cannot change the screen
  size on a real graphics card.
- IDE, SATA (AHCI) and NVMe disks.
- Intel HDA and AC'97 sound.
- Not yet: the network (there is only a virtio-net driver), more than
  one CPU core, and RAM above 4 GiB.

### Report a problem

Open an issue at <https://github.com/lassedtu/ginnOS/issues>. Write the
PC or the main board, the CPU, the graphics card, BIOS or UEFI, i686 or
x86_64, and what you saw. A photo of the screen helps if ginnOS stops
with a panic: the panic message shows a backtrace.
