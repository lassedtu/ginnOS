#include "syscall_internal.h"
#include "syscall.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/process/process.h"
#include "version.h" // made by the Makefile: GINNOS_RELEASE, GINNOS_VERSION

/** the layout of struct utsname in <sys/utsname.h> */
#define UTS_FIELD 65

typedef struct
{
    char sysname[UTS_FIELD];
    char nodename[UTS_FIELD];
    char release[UTS_FIELD];
    char version[UTS_FIELD];
    char machine[UTS_FIELD];
} uts_t;

// the host name (sethostname; the uname nodename)
static char host_name[UTS_FIELD] = "ginnos";

/**
 * SYS_sethostname: set the host name (root only).
 * args: the name, its length (at most 64 bytes, no NUL needed).
 * returns 0, or -EPERM, -EINVAL (too long), -EFAULT.
 */
syscall_ret_t sys_sethostname(struct registers *regs)
{
    uint32_t length = (uint32_t)syscall_arg(regs, 1);
    if (process_current()->euid != 0)
        return -EPERM;
    if (length >= UTS_FIELD)
        return -EINVAL;
    char name[UTS_FIELD];
    if (kerr_failed(copy_from_user(name, (const void *)syscall_arg(regs, 0), length)))
        return -EFAULT;
    name[length] = '\0';
    memcpy(host_name, name, sizeof(name));
    return 0;
}

/**
 * SYS_uname: the name of the system, its release and version, and the
 * machine.
 * args: the user's struct utsname.
 * returns 0, or a negative errno.
 */
syscall_ret_t sys_uname(struct registers *regs)
{
    void *out = (void *)syscall_arg(regs, 0);
    uts_t uts;
    memset(&uts, 0, sizeof(uts));
    strncpy(uts.sysname, "ginnOS", UTS_FIELD - 1);
    strncpy(uts.nodename, host_name, UTS_FIELD - 1);
    strncpy(uts.release, GINNOS_RELEASE, UTS_FIELD - 1);
    strncpy(uts.version, GINNOS_VERSION, UTS_FIELD - 1);
#ifdef __x86_64__
    strncpy(uts.machine, "x86_64", UTS_FIELD - 1);
#else
    strncpy(uts.machine, "i686", UTS_FIELD - 1);
#endif
    return kerr_to_errno(copy_to_user(out, &uts, sizeof(uts)));
}
