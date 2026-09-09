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
    case 0x10: /* SDLR - low byte of button data */
    case 0x14: /* SDHR - high byte of button data */
        /* Report the CURRENT pad, not a snapshot taken when the game last
         * wrote SCR_START. The VB can also read the pad on its own (the
         * hardware-initiated read enabled through SCR), and a game that relies
         * on that never writes START at all -- so `latched` stayed 0 for the
         * whole run and every button read returned zero.
         *
         * Jack Bros is exactly that game, and it does not merely miss input:
         * sub_07F00222 composes SDHR<<8 | SDLR into an event word, stores the
         * RISING EDGES of it as the kernel's dispatch flags at 20[r25], and the
         * kernel calls the task scheduler only when those flags are set. A pad
         * frozen at zero has no edges, so the flags were written three times in
         * a 700-frame run and always as 0, the scheduler ran 3 times, and every
         * task that loads graphics never got to run at all.
         *
         * Latching adds nothing here: `buttons` only changes once per frame, so
         * the two halves of a read are consistent either way. */
        latched = buttons;
        return addr == 0x10 ? (uint8_t)(latched & 0xFF)
                            : (uint8_t)(latched >> 8);
    case 0x28: /* SCR */
        /* STAT=0 means ready (we latch instantly, so a read is never pending).
         *
         * SCR_START reads back SET regardless of what was last written. Two
         * different idioms start a pad read, and only one of them survives a
         * register that reads back exactly what was stored:
         *
         *   Waterworld:   ld.b SCR; ori  0x0084; st.b SCR   -- sets the bit
         *   Mario Clash:  ld.h SCR; andi 0x0084; st.h SCR   -- KEEPS the bit
         *
         * Mario Clash's read-modify-write only starts a read if the bit is
         * already there. Its init leaves SCR at 0x80, so `0x80 & 0x84` wrote
         * 0x80 forever, the pad was never latched, and the game sat on its
         * warning screen reading zero buttons 161,598 times a run while its
         * state machine waited at state 2 for `pad & 0x303C0000`. */
        return (scr | SCR_START) & ~SCR_STAT;
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
