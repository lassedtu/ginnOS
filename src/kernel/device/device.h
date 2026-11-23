#pragma once

/**
 * @file device.h
 * @brief generic device model: a named/typed handle plus a registry.
 *
 * drivers wrap their concrete device in a device_t and register it here during
 * init, so the rest of the kernel has one place to enumerate what hardware is
 * present. this is the groundwork for a future devfs that surfaces registered
 * devices as /dev/hda, /dev/tty0, and so on.
 *
 * the model intentionally stays thin: it does not replace the driver-specific
 * init flow (which has its own ordering), it just records the result.
 */

#include "common/stdint.h"
#include "arch/arch.h"
#include "common/error.h"

// broad device classes, enough to group the registry today.
typedef enum
{
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_BLOCK,   // block storage (disk, partition)
    DEVICE_TYPE_CHAR,    // character stream (tty, serial)
    DEVICE_TYPE_INPUT,   // input source (keyboard)
    DEVICE_TYPE_TIMER,   // timer/clock source
} device_type_t;

// longest device name the registry stores (including the null terminator).
#define DEVICE_NAME_MAX 16

struct device;
typedef struct device device_t;
struct file;

/**
 * optional per-device operations. all fields may be NULL; the model does not
 * require any of them. concrete device families extend behaviour through their
 * own interfaces (block_device_t, tty_t, ...) and use this only for lifecycle.
 */
typedef struct
{
    void (*shutdown)(device_t *dev); // quiesce the device (optional)

    /**
     * device-specific control operation, the transport for ioctl(2)-style
     * requests (e.g. the framebuffer's FBIOGET_VSCREENINFO). optional: a NULL
     * hook means the device supports no ioctls.
     * @param dev the device.
     * @param request driver-defined request code.
     * @param arg request-specific argument (often a user pointer, already
     *            bounds-checked by the syscall layer before it reaches here).
     * @return 0 on success, or a negative errno on failure (-ENOTTY for an
     *         unrecognized request).
     */
    int32_t (*ioctl)(device_t *dev, uint32_t request, void *arg);

    /**
     * map the device's memory into a process address space (optional; NULL
     * means the device is not mappable, i.e. -ENODEV). the device maps its own
     * frames at @p virt in @p as; the caller (SYS_mmap) has already
     * reserved the virtual range and validated it. eager mapping, no demand
     * paging. the frames are device memory (outside PMM), so process teardown
     * leaves them alone.
     * @param dev the device.
     * @param as the target process's address space.
     * @param virt page-aligned destination virtual address.
     * @param length number of bytes to map (page-multiple).
     * @param prot PROT_* protection bits.
     * @param offset byte offset into the device memory (page-aligned).
     * @return 0 on success, or a negative errno on failure.
     */
    int32_t (*mmap)(device_t *dev, addr_space_t as, uintptr_t virt,
                    size_t length, int prot, uint32_t offset);

    /**
     * byte-oriented read at an explicit offset (optional; NULL means the device
     * is not readable). the caller (devfs) owns the cursor and passes it in.
     * @return number of bytes read (may be short at end of device).
     */
    uint32_t (*read)(device_t *dev, uint32_t offset, void *buf, uint32_t len);

    /**
     * byte-oriented write at an explicit offset (optional; NULL means the
     * device is not writable).
     * @return number of bytes written.
     */
    uint32_t (*write)(device_t *dev, uint32_t offset, const void *buf, uint32_t len);

    /**
     * total byte size of the device's addressable memory (optional; NULL means
     * unknown/zero). used for read clamping and lseek(SEEK_END).
     */
    uint32_t (*size)(device_t *dev);
    /**
     * open the device as a stream of its own (optional). when set, open(2)
     * of the /dev node gives the file this hook makes, with the device's
     * own file operations (blocking, O_NONBLOCK, poll, a release when the
     * last descriptor closes), and read, write and ioctl above are not
     * used for it.
     * @param dev the device.
     * @param flags the O_ access mode and status flags of the open.
     * @param out receives the new open-file description.
     * @return KERR_OK, or an error (KERR_BUSY if the device is in use).
     */
    kerr_t (*open)(device_t *dev, uint32_t flags, struct file **out);
} device_ops_t;

/**
 * a registered device.
 */
struct device
{
    char name[DEVICE_NAME_MAX]; // stable name, e.g. "hda", "tty0", "kbd"
    device_type_t type;         // device class
    const device_ops_t *ops;    // optional lifecycle ops (may be NULL)
    void *driver_data;          // the concrete device the driver owns
    uint32_t number;            // the device number st_rdev shows (DEVICE_NUMBER), or 0
};

/**
 * a device number from its major and minor numbers, packed as Linux and
 * the libc's makedev (sys/sysmacros.h) pack them. ginnOS uses Linux's
 * numbers for the terminals: tty0 4:0, ptmx 5:2, pts/N 136:N.
 */
#define DEVICE_NUMBER(major, minor) \
    ((((major) & 0xfffu) << 8) | ((minor) & 0xffu) | (((minor) & 0xfff00u) << 12))

/**
 * reset the device registry. call once during early boot.
 */
void device_registry_init(void);

/**
 * register a device. the device_t is owned by the caller and must outlive its
 * registration (drivers use static or long-lived storage).
 * @param dev the device to add.
 * @return true on success, false if the registry is full or dev is invalid.
 */
bool device_register(device_t *dev);

/**
 * number of currently registered devices.
 */
uint32_t device_count(void);

/**
 * get a registered device by index (0 .. device_count()-1).
 * @return the device, or NULL if the index is out of range.
 */
device_t *device_get(uint32_t index);

/**
 * find a registered device by name.
 * @return the device, or NULL if no device has that name.
 */
device_t *device_find(const char *name);
