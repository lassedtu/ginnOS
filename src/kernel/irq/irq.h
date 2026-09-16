#pragma once

/**
 * @file irq.h
 * @brief kernel IRQ handler manager: shared lines, per-handler control.
 *
 * sits on top of the arch-neutral IRQ primitives (arch/arch_irq.h). where the
 * arch layer allows one handler per line, this manager keeps a list of
 * handlers per line so several devices can share an IRQ, lets each handler be
 * enabled/disabled independently, and tracks a name per handler for debugging.
 *
 * drivers call irq_request()/irq_free() instead of arch_irq_register()
 * directly. the manager owns line masking: a line is unmasked while it has at
 * least one enabled handler and masked otherwise.
 */

#include "common/stdint.h"
#include "arch/arch_irq.h"

// how many handlers may share a single IRQ line.
#define IRQ_MAX_SHARED 4

// opaque handle to a registered handler; NULL means "no handler".
typedef struct irq_handler_entry irq_handle_t;

// flags for irq_request(). currently only the default (0) is defined; the
// argument exists so future options (edge/level, exclusive, etc.) don't change
// the signature.
#define IRQ_FLAG_NONE 0u

/**
 * initialize the IRQ manager. must run before any irq_request() call.
 */
void irq_manager_init(void);

/**
 * register a handler on an IRQ line, sharing it with any existing handlers.
 * the handler starts enabled and the line is unmasked automatically.
 * @param irq the IRQ line.
 * @param handler the function to call when the line fires.
 * @param flags reserved for future use; pass IRQ_FLAG_NONE.
 * @param name short human-readable owner name (for debugging), may be NULL.
 * @return a handle to the registration, or NULL if the line is full/invalid.
 */
irq_handle_t *irq_request(uint32_t irq, irq_handler_fn handler, uint32_t flags, const char *name);

/**
 * remove a previously requested handler. if it was the last enabled handler
 * on its line, the line is masked.
 * @param handle the handle returned by irq_request() (NULL is ignored).
 */
void irq_free(irq_handle_t *handle);

/**
 * enable or disable a single handler without removing it.
 * masks the line when no enabled handlers remain, unmasks it otherwise.
 * @param handle the handler to toggle.
 * @param enabled true to enable, false to disable.
 */
void irq_handler_set_enabled(irq_handle_t *handle, bool enabled);

/**
 * print the registered handlers per line (debugging aid).
 */
void irq_dump(void);
