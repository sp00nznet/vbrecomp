/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Hardware timer implementation.
 */

#include "vbrecomp/timer.h"

/* Timer control register bits */
#define TCR_ENABLE  (1 << 0)   /* Timer enable */
#define TCR_ZSTAT   (1 << 1)   /* Zero status (read-only, set on underflow) */
#define TCR_ZCLR    (1 << 2)   /* Clear zero status (write-only) */
#define TCR_INT     (1 << 3)   /* Interrupt enable */
#define TCR_TCLK    (1 << 4)   /* Clock select: 0 = ~100us, 1 = ~20us */

static uint8_t  tcr;           /* Timer control */
static uint16_t reload;        /* Reload value */
static uint16_t counter;       /* Current count */
static uint32_t prescaler;     /* Sub-tick accumulator */

/* CPU clock: 20MHz. Timer ticks at either ~100us (2000 cycles) or ~20us (400 cycles) */
#define TICK_RATE_SLOW  2000
#define TICK_RATE_FAST  400

void vb_timer_init(void) {
    tcr = 0;
    reload = 0;
    counter = 0;
    prescaler = 0;
}

uint8_t vb_timer_read8(vb_addr_t addr) {
    switch (addr) {
    case 0x10: return tcr;
    case 0x14: return (uint8_t)(counter & 0xFF);
    case 0x18: return (uint8_t)(counter >> 8);
    default:   return 0;
    }
}

void vb_timer_write8(vb_addr_t addr, uint8_t val) {
    switch (addr) {
    case 0x10: /* TCR */
        if (val & TCR_ZCLR) {
            tcr &= ~TCR_ZSTAT;
        }
        tcr = (tcr & TCR_ZSTAT) | (val & ~(TCR_ZSTAT | TCR_ZCLR));
        if (val & TCR_ENABLE) {
            /* Starting timer: load counter from reload value */
            counter = reload;
            prescaler = 0;
        }
        break;
    case 0x14: /* TLR (low byte of reload) */
        reload = (reload & 0xFF00) | val;
        break;
    case 0x18: /* THR (high byte of reload) */
        reload = (reload & 0x00FF) | ((uint16_t)val << 8);
        break;
    default:
        break;
    }
}

bool vb_timer_tick(uint32_t cycles) {
    if (!(tcr & TCR_ENABLE)) return false;

    uint32_t tick_rate = (tcr & TCR_TCLK) ? TICK_RATE_FAST : TICK_RATE_SLOW;
    prescaler += cycles;

    bool fired = false;
    while (prescaler >= tick_rate) {
        prescaler -= tick_rate;

        if (counter == 0) {
            /* Underflow: reload and set status */
            counter = reload;
            tcr |= TCR_ZSTAT;
            if (tcr & TCR_INT) {
                fired = true;
            }
        } else {
            counter--;
        }
    }
    return fired;
}
