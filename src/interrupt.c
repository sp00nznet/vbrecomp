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
#include "vbrecomp/timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Approximate CPU cycles per interrupt_check call. The VB runs ~400K cycles
 * per ~50Hz frame; with the default ~20K checks per frame that's ~20 cycles
 * each. Used to advance the hardware timer at roughly the right rate. */
#define VB_CYCLES_PER_CHECK 20

#define MAX_INT_LEVELS 5

static uint32_t pending;  /* Bitmask of pending interrupt levels */
/* The VIP interrupt line is LEVEL-triggered on INTPND & INTENB: it stays
 * asserted while any enabled source is still pending. We raise several sources
 * at once per frame (GAMESTART, XPEND, FBEND...), but a game's handler services
 * ONE of them and clears only that bit -- so requesting the interrupt once per
 * frame drops every other source on the floor. Jack Bros' handler took its XPEND
 * branch every time and reached its GAMESTART branch, which sets the frame flag
 * its main loop spins on, exactly once in 300 frames. Re-request while sources
 * remain, bounded so a handler that clears nothing cannot livelock us. */
static int vip_reentry;
static int vip_reentry_max = -1;   /* VBRECOMP_VIP_REENTRY, default 8 */
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
    /* Optional boot-diagnosis heartbeat (VBRECOMP_HEARTBEAT=1): periodically
     * dump the key hardware registers so a stalled boot reveals its state. */
    {
        static int hb_on = -1;
        static uint64_t hb = 0;
        if (hb_on < 0) { const char *e = getenv("VBRECOMP_HEARTBEAT"); hb_on = (e && e[0] && e[0] != '0'); }
        if (hb_on && (++hb % 4000000ull == 0)) {
            fprintf(stderr, "HB: INTPND=%04X INTENB=%04X DPSTTS=%04X XPSTTS=%04X SCR=%02X TCR=%02X SP=%08X GP=%08X\n",
                    vb_mem_read16(0x0005F800), vb_mem_read16(0x0005F802),
                    vb_mem_read16(0x0005F820), vb_mem_read16(0x0005F840),
                    vb_mem_read8(0x02000028), vb_mem_read8(0x02000020),
                    vb_cpu.r[3], vb_cpu.r[4]);
            fprintf(stderr, "  r7=%08X r25=%08X r26=%08X r31=%08X\n",
                    vb_cpu.r[7], vb_cpu.r[25], vb_cpu.r[26], vb_cpu.r[31]);
            fprintf(stderr, "  last block entered: 0x%08X\n", vb_recomp_pc);
            extern void vb_mem_dump_hot_reads(void);
            vb_mem_dump_hot_reads();
            extern void vb_mem_dump_write_watch(void);
            vb_mem_dump_write_watch();
            extern void vb_vip_dump_write_stats(void);
            vb_vip_dump_write_stats();
            fprintf(stderr, "  BRT A/B/C=%02X/%02X/%02X GPLT0=%04X DPCTRL=%04X XPCTRL=%04X\n",
                    vb_mem_read8(0x0005F824), vb_mem_read8(0x0005F826), vb_mem_read8(0x0005F828),
                    vb_mem_read16(0x0005F860), vb_mem_read16(0x0005F822), vb_mem_read16(0x0005F842));
            extern void vb_vip_dump_render_chain(void);
            vb_vip_dump_render_chain();
            vb_recomp_dump_stubs();
            /* VBRECOMP_TRACE=1: the last functions entered. A game that spins
             * in one function's wait loop shows only the interrupt chain here;
             * seeing its per-frame worker appear is what says the loop is
             * actually being released rather than merely re-armed. */
            if (getenv("VBRECOMP_TRACE")) vb_recomp_dump_trace();
            /* VBRECOMP_PEEK=<hex>[,<hex>...]: print those addresses as 32-bit
             * words. The hot-read list says which address a stalled game is
             * obsessed with and VBRECOMP_WATCH says who reads it, but neither
             * shows the VALUE -- and for a state-machine variable the value is
             * the whole question: a state that climbs is a game making
             * progress, a state that sticks is a handler that never hands off. */
            {
                const char *pk = getenv("VBRECOMP_PEEK");
                while (pk && *pk) {
                    char *end;
                    uint32_t a = (uint32_t)strtoul(pk, &end, 16);
                    if (end == pk) break;
                    fprintf(stderr, "  peek [%08X] = %08X\n", a, vb_mem_read32(a));
                    pk = (*end == ',') ? end + 1 : end;
                }
            }
        }
    }

    /* Advance the hardware timer every check (independent of frame timing).
     * Counts down whenever enabled, sets ZSTAT on underflow, and requests the
     * timer interrupt if the game enabled it. Games commonly poll or wait on
     * this during boot, so it must always run. */
    if (vb_timer_tick(VB_CYCLES_PER_CHECK)) {
        vb_interrupt_request(VB_INT_TIMER);
    }

    /* Advance frame tick counter */
    if (frame_ticks > 0) {
        tick_counter++;
        if (tick_counter >= frame_ticks) {
            tick_counter = 0;

            /* Advance VIP state (sets interrupt pending flags in INTPND) */
            vip_reentry = 0;
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

    /* Check each level, highest priority first. On the VB a HIGHER level
     * number is HIGHER priority (key=0, timer=1, expansion=2, link=3, VIP=4),
     * so scan 4 -> 0 and service the highest pending level. */
    for (int i = MAX_INT_LEVELS - 1; i >= 0; i--) {
        if (!(pending & (1u << i))) continue;

        /* V810: PSW.I is the masking threshold. An interrupt is acknowledged
         * only when its level is >= PSW.I; lower-level (lower-priority)
         * requests are held pending. (Accepting level i sets PSW.I = i+1
         * below, so same-level requests don't re-enter and only strictly
         * higher levels preempt.) The old code inverted this and blocked
         * level >= PSW.I, which masked the high-priority VIP interrupt
         * whenever a game raised PSW.I above 0 -- deadlocking any game that
         * waits on VIP from a raised interrupt level (e.g. Waterworld). */
        if (i < current_level) continue;

        /* No handler registered for this level: acknowledge and move on WITHOUT
         * touching PSW. Setting EP/ID here with nothing to run+RETI wedges the
         * CPU -- EP stays set forever, so every subsequent interrupt is blocked
         * and the game deadlocks (it accepted a phantom interrupt it can never
         * return from). This happens when the recompiler didn't discover the
         * game's handlers (vb_recomp_init_handlers empty). */
        if (!handlers[i]) {
            pending &= ~(1u << i);
            continue;
        }

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

        uint16_t vip_before = (i == VB_INT_VIP)
            ? (uint16_t)(vb_mem_read16(0x0005F800) & vb_mem_read16(0x0005F802)) : 0;
        /* Call handler if registered */
        if (handlers[i]) {
            static int int_count[MAX_INT_LEVELS] = {0};
            int_count[i]++;
            if (int_count[i] <= 3 || int_count[i] % 100 == 0) {
                fprintf(stderr, "INT: level %d fired (count=%d, PSW was 0x%08X)\n",
                        i, int_count[i], psw);
            }
            uint32_t sp_before = vb_cpu.r[3];
            /* Only pay for the snapshot when someone is looking. */
            static int reg_chk = -1;
            if (reg_chk < 0) reg_chk = getenv("VBRECOMP_HEARTBEAT") ? 1 : 0;
            uint32_t regs_before[32];
            if (reg_chk) memcpy(regs_before, vb_cpu.r, sizeof(regs_before));
            /* vb_interrupt_check runs at the top of every generated function,
              * including inside a handler, so a handler that re-enables
              * interrupts can nest. The level rule bounds that to one frame per
              * level -- if this counter climbs past that, the bound is broken
              * and the C stack is what pays. */
             {
                 static int nest, warned;
                 nest++;
                 if (nest > 8 && warned < 6) {
                     warned++;
                     fprintf(stderr, "IRQ NEST %d at level %d (PSW=%08X)\n",
                             nest, i, vb_cpu_get_psw());
                 }
                 handlers[i]();
                 nest--;
             }
            /* A handler that ends in a jump parks its target; drain it here, or
             * the transfer fires later at an unrelated call site. */
            vb_recomp_drain();
            if (vb_cpu.r[3] != sp_before) {
                static int leak_log[MAX_INT_LEVELS] = {0};
                if (getenv("VBRECOMP_HEARTBEAT") && leak_log[i]++ < 5)
                    fprintf(stderr, "IRQ %d LEAKED SP: %08X -> %08X (delta %d)\n",
                            i, sp_before, vb_cpu.r[3], (int)(vb_cpu.r[3] - sp_before));
            }
            /* A handler must leave the guest's registers as it found them.
             * When our translation loses part of an epilogue it does not, and
             * the damage lands in whatever the interrupted code was holding --
             * Galactic Pinball's main loop counts down r25 from 6 and finds it
             * at 0xFFFFFF9F, so its inner loop runs four billion times. */
            {
                static int reg_log[MAX_INT_LEVELS] = {0};
                if (reg_chk && reg_log[i] < 3) {
                    for (int r = 2; r < 31; r++) {
                        if (r == 3) continue;   /* SP reported above */
                        if (vb_cpu.r[r] != regs_before[r]) {
                            reg_log[i]++;
                            fprintf(stderr, "IRQ %d CLOBBERED r%d: %08X -> %08X\n",
                                    i, r, regs_before[r], vb_cpu.r[r]);
                            break;
                        }
                    }
                }
            }
            if (0) {
            }
        }
        if (vip_reentry_max < 0) {
            const char *e = getenv("VBRECOMP_VIP_REENTRY");
            vip_reentry_max = e ? atoi(e) : 8;
        }
        if (i == VB_INT_VIP && vip_reentry < vip_reentry_max) {
            uint16_t after = vb_mem_read16(0x0005F800) & vb_mem_read16(0x0005F802);
            /* Only when the handler CLEARED something and sources remain. A
             * handler that cleared nothing is not servicing sources one at a
             * time -- re-entering it just runs the same work again, which is
             * what broke Bound High and Virtual Bowling. */
            if (after && after != vip_before) {
                vip_reentry++;
                vb_interrupt_request(VB_INT_VIP);
            }
        }
        return; /* Handle one interrupt at a time */
    }
}

void vb_interrupt_set_handler(int level, vb_int_handler_t handler) {
    if (level >= 0 && level < MAX_INT_LEVELS) {
        handlers[level] = handler;
    }
}
