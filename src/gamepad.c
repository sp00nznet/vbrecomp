/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Game pad input.
 * The VB reads the controller via a serial interface. Games write to SCR
 * to start a read, then poll SCR for completion and read SDR/SDHR for data.
 * We skip the serial protocol and just provide instant button data.
 */

#include "vbrecomp/gamepad.h"

/* SCR bits */
#define SCR_STAT    (1 << 1)   /* Read status: 0 = ready */
#define SCR_START   (1 << 2)   /* Start read */
#define SCR_RESET   (1 << 3)   /* Reset controller */
#define SCR_ABORT   (1 << 4)   /* Abort read */
#define SCR_SI      (1 << 5)   /* Software interrupt enable */
#define SCR_HWSI    (1 << 6)   /* Hardware SI enable */

static uint8_t  scr;
static uint16_t buttons;   /* Current button state from platform */
static uint16_t latched;   /* Button state latched on read start */

void vb_gamepad_init(void) {
    scr = 0;
    buttons = 0;
    latched = 0;
}

uint8_t vb_gamepad_read8(vb_addr_t addr) {
    switch (addr) {
    case 0x28: /* SCR */
        return scr & ~SCR_STAT; /* STAT=0 means ready (instant read) */
    case 0x2C: /* SDR - low byte of button data */
        return (uint8_t)(latched & 0xFF);
    case 0x30: /* SDHR - high byte of button data */
        return (uint8_t)(latched >> 8);
    default:
        return 0;
    }
}

void vb_gamepad_write8(vb_addr_t addr, uint8_t val) {
    switch (addr) {
    case 0x28: /* SCR */
        scr = val;
        if (val & SCR_START) {
            /* Latch current button state immediately */
            latched = buttons;
        }
        if (val & SCR_RESET) {
            latched = 0;
        }
        break;
    default:
        break;
    }
}

void vb_gamepad_set_buttons(uint16_t btn) {
    buttons = btn;
}
