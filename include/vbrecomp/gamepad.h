/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * gamepad.h - Game pad input
 *
 * The VB controller has: left d-pad, right d-pad, A, B, L, R, Start, Select.
 * Hardware reads via serial protocol; we just provide the button state.
 *
 * Registers at 0x02000028-0x02000030:
 *   SCR  (0x28): Serial control (start read, status)
 *   SDR  (0x2C): Serial data low
 *   SDHR (0x30): Serial data high (extra bits, typically unused)
 */

#ifndef VBRECOMP_GAMEPAD_H
#define VBRECOMP_GAMEPAD_H

#include "types.h"

void vb_gamepad_init(void);

/* Register access from the bus */
uint8_t vb_gamepad_read8(vb_addr_t addr);
void vb_gamepad_write8(vb_addr_t addr, uint8_t val);

/* Set current button state (use VB_BTN_* bits from types.h) */
void vb_gamepad_set_buttons(uint16_t buttons);

#endif /* VBRECOMP_GAMEPAD_H */
