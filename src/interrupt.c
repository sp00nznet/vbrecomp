/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Interrupt controller for static recompilation.
 *
 * In a normal emulator, interrupts are checked between instructions.
 * In static recomp, we check at function boundaries and loop headers.
 */

#include "vbrecomp/interrupt.h"
#include "vbrecomp/cpu.h"
#include <string.h>

#define MAX_INT_LEVELS 5

static uint32_t pending;  /* Bitmask of pending interrupt levels */
static vb_int_handler_t handlers[MAX_INT_LEVELS];

void vb_interrupt_init(void) {
    pending = 0;
    memset(handlers, 0, sizeof(handlers));
}

void vb_interrupt_request(int level) {
    if (level >= 0 && level < MAX_INT_LEVELS) {
        pending |= (1u << level);
    }
}

void vb_interrupt_clear(int level) {
    if (level >= 0 && level < MAX_INT_LEVELS) {
        pending &= ~(1u << level);
    }
}

void vb_interrupt_check(void) {
    if (pending == 0) return;

    uint32_t psw = vb_cpu_get_psw();

    /* Interrupts disabled? */
    if (psw & VB_PSW_ID) return;
    /* Exception or NMI pending blocks regular interrupts */
    if (psw & (VB_PSW_EP | VB_PSW_NP)) return;

    /* Current interrupt level from PSW */
    int current_level = (psw & VB_PSW_I_MASK) >> VB_PSW_I_SHIFT;

    /* Check each level, highest priority (0) first */
    for (int i = 0; i < MAX_INT_LEVELS; i++) {
        if (!(pending & (1u << i))) continue;

        /* V810 interrupt levels map to priority: the pending interrupt's
         * priority must be higher than the current masking level.
         * Lower interrupt number = higher priority. */
        if (i >= current_level) continue;

        /* Accept the interrupt */
        pending &= ~(1u << i);

        /* Save state */
        vb_cpu.sr[VB_SREG_EIPC] = vb_cpu.pc;
        vb_cpu.sr[VB_SREG_EIPSW] = psw;

        /* Update PSW: set EP, disable interrupts, set masking level */
        psw |= VB_PSW_EP | VB_PSW_ID;
        psw = (psw & ~VB_PSW_I_MASK) | ((uint32_t)(i + 1) << VB_PSW_I_SHIFT);
        vb_cpu_set_psw(psw);

        /* Set ECR with interrupt code */
        vb_cpu.sr[VB_SREG_ECR] = 0xFE00 + (i * 0x10);

        /* Call handler if registered */
        if (handlers[i]) {
            handlers[i]();
        }
        return; /* Handle one interrupt at a time */
    }
}

void vb_interrupt_set_handler(int level, vb_int_handler_t handler) {
    if (level >= 0 && level < MAX_INT_LEVELS) {
        handlers[level] = handler;
    }
}
