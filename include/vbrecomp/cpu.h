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

/* ============================================================
 * Recompiler ALU / flag helpers
 *
 * The recompiled code calls these instead of inlining PSW flag
 * arithmetic for every instruction, so the generated functions stay
 * readable (e.g. `vb_cmp(a, b);` instead of an 8-line flag block).
 * Each updates the PSW exactly as the V810 hardware does.
 * ============================================================ */

/* ADD: a + b, sets Z/S/OV/CY. */
static inline uint32_t vb_add(uint32_t a, uint32_t b) {
    uint64_t r64 = (uint64_t)a + (uint64_t)b;
    uint32_t r = (uint32_t)r64;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);
    if (r == 0) psw |= VB_PSW_Z;
    if (r & 0x80000000) psw |= VB_PSW_S;
    if (r64 > 0xFFFFFFFFu) psw |= VB_PSW_CY;
    if (((a ^ r) & (b ^ r)) & 0x80000000) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return r;
}

/* SUB: a - b, sets Z/S/OV/CY (borrow). */
static inline uint32_t vb_sub(uint32_t a, uint32_t b) {
    uint32_t r = a - b;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);
    if (r == 0) psw |= VB_PSW_Z;
    if (r & 0x80000000) psw |= VB_PSW_S;
    if (b > a) psw |= VB_PSW_CY;
    if (((a ^ b) & (a ^ r)) & 0x80000000) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return r;
}

/* CMP: SUB that discards the result, keeping only the flags. */
static inline void vb_cmp(uint32_t a, uint32_t b) { (void)vb_sub(a, b); }

/* Logical ops (AND/OR/XOR/NOT): set Z/S from result, clear OV. */
static inline uint32_t vb_setf_logic(uint32_t v) {
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    if (v == 0) psw |= VB_PSW_Z;
    if (v & 0x80000000) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return v;
}

/* SHL: logical left shift by (sh & 31), CY = last bit shifted out. */
static inline uint32_t vb_shl(uint32_t v, uint32_t sh) {
    sh &= 0x1F;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);
    if (sh > 0) { if ((v >> (32 - sh)) & 1) psw |= VB_PSW_CY; v <<= sh; }
    if (v == 0) psw |= VB_PSW_Z;
    if (v & 0x80000000) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return v;
}

/* SHR: logical right shift by (sh & 31), CY = last bit shifted out. */
static inline uint32_t vb_shr(uint32_t v, uint32_t sh) {
    sh &= 0x1F;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);
    if (sh > 0) { if ((v >> (sh - 1)) & 1) psw |= VB_PSW_CY; v >>= sh; }
    if (v == 0) psw |= VB_PSW_Z;
    if (v & 0x80000000) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return v;
}

/* SAR: arithmetic right shift by (sh & 31), CY = last bit shifted out. */
static inline uint32_t vb_sar(uint32_t v, uint32_t sh) {
    sh &= 0x1F;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);
    if (sh > 0) { if ((v >> (sh - 1)) & 1) psw |= VB_PSW_CY; v = (uint32_t)((int32_t)v >> sh); }
    if (v == 0) psw |= VB_PSW_Z;
    if (v & 0x80000000) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return v;
}

/* MUL: signed 32x32 -> 64; low word returned, high word into r30. */
static inline uint32_t vb_mul(uint32_t a, uint32_t b) {
    int64_t r64 = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
    vb_cpu.r[30] = (uint32_t)((uint64_t)r64 >> 32);
    uint32_t lo = (uint32_t)r64;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    if (lo == 0) psw |= VB_PSW_Z;
    if (r64 & 0x80000000) psw |= VB_PSW_S;
    if (r64 > 0x7FFFFFFF || r64 < -(int64_t)0x80000000) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return lo;
}

/* MULU: unsigned 32x32 -> 64; low word returned, high word into r30. */
static inline uint32_t vb_mulu(uint32_t a, uint32_t b) {
    uint64_t r64 = (uint64_t)a * (uint64_t)b;
    vb_cpu.r[30] = (uint32_t)(r64 >> 32);
    uint32_t lo = (uint32_t)r64;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    if (lo == 0) psw |= VB_PSW_Z;
    if (r64 & 0x80000000) psw |= VB_PSW_S;
    if (r64 > 0xFFFFFFFFu) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return lo;
}

/* DIV: signed quotient returned, remainder into r30. Caller guards b != 0. */
static inline uint32_t vb_div(uint32_t a, uint32_t b) {
    int32_t q = (int32_t)a / (int32_t)b;
    vb_cpu.r[30] = (uint32_t)((int32_t)a % (int32_t)b);
    vb_cpu_set_flags_zs((uint32_t)q);
    return (uint32_t)q;
}

/* DIVU: unsigned quotient returned, remainder into r30. Caller guards b != 0. */
static inline uint32_t vb_divu(uint32_t a, uint32_t b) {
    vb_cpu.r[30] = a % b;
    uint32_t q = a / b;
    vb_cpu_set_flags_zs(q);
    return q;
}

#endif /* VBRECOMP_CPU_H */
