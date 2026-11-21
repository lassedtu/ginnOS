#include "backtrace.h"

#include "arch/arch.h"
#include "common/stdio.h"
#include "kernel/klog/klog.h"
#include "kernel/debug/ksyms.h"

// print the same line to the console and to the serial log.
#define BT_LINE(fmt, ...)                         \
    do                                            \
    {                                             \
        printf(fmt "\r\n", __VA_ARGS__);          \
        KLOG_ERROR(fmt, __VA_ARGS__);             \
    } while (0)

// hex digits in a pointer-width address
#define ADDR_HEX_DIGITS (2 * sizeof(uintptr_t))

// the kernel printf has no field widths, so pad addresses by hand to keep
// the columns lined up: "0001234a".
static void format_hex_addr(char out[ADDR_HEX_DIGITS + 1], uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = (int)ADDR_HEX_DIGITS - 1; i >= 0; i--)
    {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    out[ADDR_HEX_DIGITS] = '\0';
}

// print "<label> 0x<pc> name+0xoff". lookup is the address used for the
// symbol (pc itself, or pc - 1 for return addresses).
static void print_entry(const char *label, int index, uintptr_t pc, uintptr_t lookup)
{
    char hex[ADDR_HEX_DIGITS + 1];
    format_hex_addr(hex, pc);

    uintptr_t offset = 0;
    const char *name = ksym_lookup(lookup, &offset);
    if (!name)
    {
        name = "??";
    }
    offset += pc - lookup;

    if (index >= 0)
    {
        BT_LINE("  %s%d 0x%s %s+0x%lx", label, index, hex, name, offset);
    }
    else
    {
        BT_LINE("  %s 0x%s %s+0x%lx", label, hex, name, offset);
    }
}

void backtrace_print_pc(const char *label, uintptr_t pc)
{
    print_entry(label, -1, pc, pc);
}

void backtrace_print_return(const char *label, uintptr_t ret)
{
    print_entry(label, -1, ret, ret - 1);
}

void backtrace_print(uintptr_t frame)
{
    uintptr_t pcs[BACKTRACE_MAX_FRAMES];
    int depth = arch_stack_walk(frame, pcs, BACKTRACE_MAX_FRAMES);

    printf("backtrace:\r\n");
    KLOG_ERROR("backtrace:");

    for (int i = 0; i < depth; i++)
    {
        // a return address points just past the call. look up the byte
        // before it, so a call that ends a function (e.g. to a noreturn
        // function) is still attributed to the caller.
        uintptr_t pc = pcs[i];
        if (!ksym_is_kernel_text(pc - 1))
        {
            break;
        }
        print_entry("#", i, pc, pc - 1);
    }
}
