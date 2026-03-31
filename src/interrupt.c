/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Interrupt controller for static recompilation.
 *
 * In a normal emulator, interrupts are checked between instructions.
 * In static recomp, we check at function boundaries and loop headers.
 * A tick counter fires VIP frame interrupts periodically.
 */

#include "vbrecomp/interrupt.h"
#include "vbrecomp/vip.h"
#include "vbrecomp/mem.h"
#include "vbrecomp/cpu.h"
#include <string.h>
#include <stdio.h>

#define MAX_INT_LEVELS 5

static uint32_t pending;  /* Bitmask of pending interrupt levels */
static vb_int_handler_t handlers[MAX_INT_LEVELS];

/* Frame timing */
static uint32_t frame_ticks;       /* Ticks between frame events (0 = disabled) */
static uint32_t tick_counter;      /* Current tick count */
static vb_frame_callback_t frame_cb;

void vb_interrupt_init(void) {
    pending = 0;
    memset(handlers, 0, sizeof(handlers));
    frame_ticks = 20000;  /* Default: ~20K checks per frame */
    tick_counter = 0;
    frame_cb = NULL;
}

void vb_interrupt_set_frame_ticks(uint32_t ticks) {
    frame_ticks = ticks;
}

void vb_interrupt_set_frame_callback(vb_frame_callback_t cb) {
    frame_cb = cb;
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
    /* Advance frame tick counter */
    if (frame_ticks > 0) {
        tick_counter++;
        if (tick_counter >= frame_ticks) {
            tick_counter = 0;

            /* Advance VIP state (sets interrupt pending flags in INTPND) */
            vb_vip_frame_advance();

            /* Only request VIP interrupt if the game has enabled VIP interrupts.
             * The game writes to INTENB to enable, and the handler clears it
             * after processing. Check INTPND & INTENB. */
            {
                uint16_t intpnd = vb_mem_read16(0x0005F800);  /* INTPND */
                uint16_t intenb = vb_mem_read16(0x0005F802);  /* INTENB */
                if (intpnd & intenb) {
                    vb_interrupt_request(VB_INT_VIP);
                }
            }

            /* Call frame callback for rendering/input */
            if (frame_cb) {
                frame_cb();
            }
        }
    }

    if (pending == 0) return;

    uint32_t psw = vb_cpu_get_psw();

    /* Debug: log why interrupts are blocked */
    {
        static int block_count = 0;
        block_count++;
        if (block_count == 50000) {
            int il = (psw & VB_PSW_I_MASK) >> VB_PSW_I_SHIFT;
            fprintf(stderr, "INT blocked: pending=0x%X PSW=0x%08X (ID=%d EP=%d NP=%d IL=%d)\n",
                    pending, psw,
                    !!(psw & VB_PSW_ID), !!(psw & VB_PSW_EP),
                    !!(psw & VB_PSW_NP), il);
            block_count = 0;
        }
    }

    /* Interrupts disabled? */
    if (psw & VB_PSW_ID) return;
    /* Exception or NMI pending blocks regular interrupts */
    if (psw & (VB_PSW_EP | VB_PSW_NP)) return;

    /* Current interrupt level from PSW */
    int current_level = (psw & VB_PSW_I_MASK) >> VB_PSW_I_SHIFT;

    /* Check each level, highest priority (0) first */
    for (int i = 0; i < MAX_INT_LEVELS; i++) {
        if (!(pending & (1u << i))) continue;

        /* V810: IL in PSW is the masking threshold. Interrupts with
         * level >= IL are blocked. Level 0 = highest priority.
         * IL=0 means accept all interrupts. */
        if (current_level > 0 && i >= current_level) continue;

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
            static int int_count[MAX_INT_LEVELS] = {0};
            int_count[i]++;
            if (int_count[i] <= 3 || int_count[i] % 100 == 0) {
                fprintf(stderr, "INT: level %d fired (count=%d, PSW was 0x%08X)\n",
                        i, int_count[i], psw);
            }
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
