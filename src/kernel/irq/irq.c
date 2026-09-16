#include "irq.h"

#include "common/stdio.h"
#include "kernel/klog/klog.h"

/**
 * @file irq.c
 * @brief kernel IRQ handler manager.
 *
 * one dispatcher is registered with the arch layer per line; it walks that
 * line's handler list and invokes every enabled handler. handler storage is a
 * fixed pool so there is no allocation on the interrupt path.
 */

#define IRQ_LINE_COUNT 16

struct irq_handler_entry
{
    irq_handler_fn handler; // driver callback, NULL if the slot is free
    const char *name;       // owner name for debugging
    uint32_t irq;           // line this handler is attached to
    uint32_t flags;         // request flags (reserved)
    bool enabled;           // whether this handler currently receives the line
};

// per-line handler tables. a slot with handler == NULL is free.
static struct irq_handler_entry handlers[IRQ_LINE_COUNT][IRQ_MAX_SHARED];

/**
 * count how many handlers on a line are currently enabled.
 */
static int enabled_count(uint32_t irq)
{
    int n = 0;
    for (int i = 0; i < IRQ_MAX_SHARED; i++)
    {
        if (handlers[irq][i].handler && handlers[irq][i].enabled)
        {
            n++;
        }
    }
    return n;
}

/**
 * unmask the line if it has an enabled handler, mask it otherwise. keeps the
 * hardware mask in sync with the handler list without callers thinking about it.
 */
static void sync_line_mask(uint32_t irq)
{
    if (enabled_count(irq) > 0)
    {
        arch_irq_enable(irq);
    }
    else
    {
        arch_irq_disable(irq);
    }
}

/**
 * shared dispatcher installed on every line. fans the interrupt out to all
 * enabled handlers registered for that line.
 */
static void irq_dispatch(uint32_t irq, trap_frame_t *frame)
{
    if (irq >= IRQ_LINE_COUNT)
    {
        return;
    }

    for (int i = 0; i < IRQ_MAX_SHARED; i++)
    {
        struct irq_handler_entry *e = &handlers[irq][i];
        if (e->handler && e->enabled)
        {
            e->handler(irq, frame);
        }
    }
}

void irq_manager_init(void)
{
    for (uint32_t irq = 0; irq < IRQ_LINE_COUNT; irq++)
    {
        for (int i = 0; i < IRQ_MAX_SHARED; i++)
        {
            handlers[irq][i].handler = 0;
        }
        // install one dispatcher per line; lines stay masked until a handler
        // is requested.
        arch_irq_register(irq, irq_dispatch);
        arch_irq_disable(irq);
    }
}

irq_handle_t *irq_request(uint32_t irq, irq_handler_fn handler, uint32_t flags, const char *name)
{
    if (irq >= IRQ_LINE_COUNT || !handler)
    {
        return 0;
    }

    for (int i = 0; i < IRQ_MAX_SHARED; i++)
    {
        struct irq_handler_entry *e = &handlers[irq][i];
        if (!e->handler)
        {
            e->handler = handler;
            e->name = name;
            e->irq = irq;
            e->flags = flags;
            e->enabled = true;
            sync_line_mask(irq);
            return e;
        }
    }

    // no free slot on this line.
    return 0;
}

void irq_free(irq_handle_t *handle)
{
    if (!handle)
    {
        return;
    }

    uint32_t irq = handle->irq;
    handle->handler = 0;
    handle->name = 0;
    handle->enabled = false;
    sync_line_mask(irq);
}

void irq_handler_set_enabled(irq_handle_t *handle, bool enabled)
{
    if (!handle || !handle->handler)
    {
        return;
    }

    handle->enabled = enabled;
    sync_line_mask(handle->irq);
}

void irq_dump(void)
{
    for (uint32_t irq = 0; irq < IRQ_LINE_COUNT; irq++)
    {
        for (int i = 0; i < IRQ_MAX_SHARED; i++)
        {
            struct irq_handler_entry *e = &handlers[irq][i];
            if (e->handler)
            {
                printf("IRQ %u: %s (%s)\r\n",
                       irq,
                       e->name ? e->name : "?",
                       e->enabled ? "enabled" : "disabled");
            }
        }
    }
}
