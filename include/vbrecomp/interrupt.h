/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * interrupt.h - Interrupt controller
 *
 * The V810 supports 16 interrupt levels (0 = highest priority).
 * Virtual Boy uses:
 *   Level 0: Key input (gamepad)
 *   Level 1: Timer
 *   Level 2: Expansion (unused)
 *   Level 3: Link (communication)
 *   Level 4: VIP (video)
 *
 * In static recomp, we check for pending interrupts at function
 * boundaries and backward branch targets.
 */

#ifndef VBRECOMP_INTERRUPT_H
#define VBRECOMP_INTERRUPT_H

#include "types.h"

#define VB_INT_KEY      0
#define VB_INT_TIMER    1
#define VB_INT_EXPANSION 2
#define VB_INT_LINK     3
#define VB_INT_VIP      4

void vb_interrupt_init(void);

/* Request an interrupt at the given level */
void vb_interrupt_request(int level);

/* Clear a pending interrupt */
void vb_interrupt_clear(int level);

/*
 * Check and dispatch pending interrupts.
 * Call this at function entry points and loop headers in recompiled code.
 * If an interrupt fires, this calls the appropriate handler (which is
 * itself a recompiled function).
 */
void vb_interrupt_check(void);

/*
 * Set the number of interrupt_check() calls between VIP frame events.
 * The VB runs at ~50Hz with ~400K cycles/frame. Since each check
 * represents ~1-10 instructions, a good default is ~20000-50000.
 * Set to 0 to disable automatic VIP interrupts.
 */
void vb_interrupt_set_frame_ticks(uint32_t ticks);

/*
 * Frame callback: called once per frame tick for rendering/input.
 * Return false to request game exit.
 */
typedef bool (*vb_frame_callback_t)(void);
void vb_interrupt_set_frame_callback(vb_frame_callback_t cb);

/*
 * Register an interrupt handler (recompiled function) for a given vector.
 * The handler is the recompiled function at the V810 interrupt vector address.
 */
typedef void (*vb_int_handler_t)(void);
void vb_interrupt_set_handler(int level, vb_int_handler_t handler);

#endif /* VBRECOMP_INTERRUPT_H */
