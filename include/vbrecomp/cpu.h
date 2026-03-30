/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * cpu.h - V810 CPU state
 *
 * The V810 is a 32-bit RISC processor with 32 general-purpose registers
 * and a handful of system registers. r0 is hardwired to zero.
 */

#ifndef VBRECOMP_CPU_H
#define VBRECOMP_CPU_H

#include "types.h"

/* Program Status Word (PSW) flag bits */
#define VB_PSW_Z    (1 << 0)    /* Zero */
#define VB_PSW_S    (1 << 1)    /* Sign */
#define VB_PSW_OV   (1 << 2)    /* Overflow */
#define VB_PSW_CY   (1 << 3)    /* Carry */
#define VB_PSW_FPR  (1 << 4)    /* Floating point precision degradation */
#define VB_PSW_FUD  (1 << 5)    /* Floating point underflow */
#define VB_PSW_FOV  (1 << 6)    /* Floating point overflow */
#define VB_PSW_FZD  (1 << 7)    /* Floating point zero division */
#define VB_PSW_FIV  (1 << 8)    /* Floating point invalid operation */
#define VB_PSW_FRO  (1 << 9)    /* Floating point reserved operand */
#define VB_PSW_ID   (1 << 12)   /* Interrupt disable */
#define VB_PSW_AE   (1 << 13)   /* Address trap enable */
#define VB_PSW_EP   (1 << 14)   /* Exception pending */
#define VB_PSW_NP   (1 << 15)   /* NMI pending */
#define VB_PSW_I_SHIFT 16       /* Interrupt level (bits 19:16) */
#define VB_PSW_I_MASK  (0xF << VB_PSW_I_SHIFT)

/* System register indices */
#define VB_SREG_EIPC   0   /* Exception/interrupt saved PC */
#define VB_SREG_EIPSW  1   /* Exception/interrupt saved PSW */
#define VB_SREG_FEPC   2   /* Fatal error saved PC */
#define VB_SREG_FEPSW  3   /* Fatal error saved PSW */
#define VB_SREG_ECR    4   /* Exception cause register */
#define VB_SREG_PSW    5   /* Program status word */
#define VB_SREG_PIR    6   /* Processor ID register (read-only: 0x00005346) */
#define VB_SREG_TKCW   7   /* Task control word */
#define VB_SREG_CHCW  24   /* Cache control word */
#define VB_SREG_ADTRE  25  /* Address trap register */
#define VB_SREG_COUNT  32  /* Total system register slots */

typedef struct {
    uint32_t r[32];                 /* General purpose registers, r[0] = 0 always */
    uint32_t sr[VB_SREG_COUNT];     /* System registers */
    uint32_t pc;                    /* Program counter (for debugging/tracing) */
} vb_cpu_state_t;

extern vb_cpu_state_t vb_cpu;

/* Initialize CPU state (clears registers, sets PIR) */
void vb_cpu_init(void);

/* Convenience accessors for PSW (sr[5]) */
static inline uint32_t vb_cpu_get_psw(void) { return vb_cpu.sr[VB_SREG_PSW]; }
static inline void vb_cpu_set_psw(uint32_t val) { vb_cpu.sr[VB_SREG_PSW] = val; }

/* Update PSW arithmetic flags after an operation */
static inline void vb_cpu_set_flags_zs(uint32_t result) {
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z | VB_PSW_S);
    if (result == 0) psw |= VB_PSW_Z;
    if (result & 0x80000000) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
}

#endif /* VBRECOMP_CPU_H */
