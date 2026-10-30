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

// the kernel printf has no field widths, so pad addresses by hand to keep
// the columns lined up: "0001234a".
static void format_hex8(char out[9], uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--)
    {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    out[8] = '\0';
}

// print "<label> 0x<pc> name+0xoff". lookup is the address used for the
// symbol (pc itself, or pc - 1 for return addresses).
static void print_entry(const char *label, int index, uint32_t pc, uint32_t lookup)
{
    char hex[9];
    format_hex8(hex, pc);

    uint32_t offset = 0;
    const char *name = ksym_lookup(lookup, &offset);
    if (!name)
    {
        name = "??";
    }
    offset += pc - lookup;

    if (index >= 0)
    {
        BT_LINE("  %s%d 0x%s %s+0x%x", label, index, hex, name, offset);
    }
    else
    {
        BT_LINE("  %s 0x%s %s+0x%x", label, hex, name, offset);
    }
}

void backtrace_print_pc(const char *label, uint32_t pc)
{
    print_entry(label, -1, pc, pc);
}

void backtrace_print_return(const char *label, uint32_t ret)
{
    print_entry(label, -1, ret, ret - 1);
}

void backtrace_print(uint32_t frame)
{
    uint32_t pcs[BACKTRACE_MAX_FRAMES];
    int depth = arch_stack_walk(frame, pcs, BACKTRACE_MAX_FRAMES);

    printf("backtrace:\r\n");
    KLOG_ERROR("backtrace:");

    for (int i = 0; i < depth; i++)
    {
        // a return address points just past the call. look up the byte
        // before it, so a call that ends a function (e.g. to a noreturn
        // function) is still attributed to the caller.
        uint32_t pc = pcs[i];
        if (!ksym_is_kernel_text(pc - 1))
        {
            break;
        }
        print_entry("#", i, pc, pc - 1);
    }
}
