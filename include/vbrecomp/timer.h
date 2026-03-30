/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * timer.h - Hardware timer
 *
 * The VB has a single hardware timer with:
 *   - 16-bit reload value
 *   - 16-bit counter (counts down)
 *   - Prescaler: ~100us or ~20us tick rate
 *   - Can fire an interrupt on underflow
 *
 * Registers at 0x02000010-0x02000014:
 *   TCR (0x10): Timer control register
 *   TLR (0x14): Timer counter low byte
 *   THR (0x18): Timer counter high byte
 */

#ifndef VBRECOMP_TIMER_H
#define VBRECOMP_TIMER_H

#include "types.h"

void vb_timer_init(void);

/* Register access from the bus */
uint8_t vb_timer_read8(vb_addr_t addr);
void vb_timer_write8(vb_addr_t addr, uint8_t val);

/* Advance timer by the given number of CPU cycles (20MHz clock).
 * Returns true if the timer underflowed (interrupt should fire). */
bool vb_timer_tick(uint32_t cycles);

#endif /* VBRECOMP_TIMER_H */
