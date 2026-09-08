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

/* Bit string sub-opcodes (the reg2 field of opcode 0x1F). The "N" forms
 * invert the source bit before combining it with the destination. */
#define VB_BSU_OR     0x08
#define VB_BSU_AND    0x09
#define VB_BSU_XOR    0x0A
#define VB_BSU_MOV    0x0B
#define VB_BSU_ORN    0x0C
#define VB_BSU_ANDN   0x0D
#define VB_BSU_XORN   0x0E
#define VB_BSU_NOT    0x0F

typedef struct {
    uint32_t r[32];                 /* General purpose registers, r[0] = 0 always */
    uint32_t sr[VB_SREG_COUNT];     /* System registers */
    uint32_t pc;                    /* Program counter (for debugging/tracing) */
} vb_cpu_state_t;

extern vb_cpu_state_t vb_cpu;

/* Initialize CPU state (clears registers, sets PIR) */
void vb_cpu_init(void);

/* A pending TAIL JUMP: the address recompiled code transferred to instead of
 * returning.
 *
 * A game's main loop is a control-flow CYCLE. Mario's Tennis dispatches through
 * a 64-entry jump table and each state handler ends `jr` back to the
 * dispatcher; translated as calls that is unbounded mutual recursion, and
 * translated as returns the loop is dropped and the boot unwinds after one
 * state -- which is why 16 corpus ROMs ran no game code at all.
 *
 * So a jump out of a function parks its target here and returns. Every CALL
 * SITE then drains it (vb_recomp_drain, emitted alongside the dispatch table):
 * the chain runs to completion there, before the caller resumes, so ordering is
 * preserved and the stack stays flat.
 *
 * Draining at the call site rather than only at boot is the whole trick. An
 * earlier attempt returned all the way to boot, so a caller resumed before its
 * callee's transfer had run -- which broke games that had been working. */
extern uint32_t vb_recomp_tail_target;

/* Run any parked transfer to completion. Defined in the generated code (it
 * needs the dispatch table). The runtime calls this after invoking an interrupt
 * handler: a handler that ends in a jump parks its target, and with nobody
 * draining it there the transfer would fire later at an unrelated call site,
 * with unrelated state. */
void vb_recomp_drain(void);

/* Depth trace: see vb_recomp_deep in cpu.c. */
void vb_recomp_deep(uint32_t addr, int depth);

/* Bit string op (opcode 0x1F): copies r28 bits from bit r30 of [r26] to bit
 * r29 of [r27], combining with the destination per `op` (VB_BSU_*). */
void vb_bitstring(int op);

/* A call the translation could not make: an unanalyzed function's stub, a JAL
 * to a callee that was never found, or an indirect jump to an address that is
 * not in the dispatch table. Silent by nature, so they are counted instead --
 * the heartbeat prints the busiest, which names the analysis gap that is
 * keeping a game from getting anywhere. */
void vb_recomp_stub(uint32_t addr);

/* Execution trace: the recompiled code's backtrace. Every function entry
 * records itself; dump it when a boot chain ends to see what ran last. */
extern uint32_t vb_recomp_func_hits;  /* VBRECOMP_COUNT_FUNC entry counter */
extern uint32_t vb_recomp_pc;   /* address of the last basic block entered */
void vb_recomp_trace(uint32_t addr);
void vb_recomp_dump_trace(void);
void vb_recomp_dump_stubs(void);

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

/* MUL: signed 32x32 -> 64; low word returned, high word into r30.
 *
 * Z and S describe the whole 64-bit product, not the low word: 0x10000 squared
 * is 0x1_00000000, whose low word is zero, and reading Z off that word alone
 * tells the game its nonzero product was zero. */
static inline uint32_t vb_mul(uint32_t a, uint32_t b) {
    int64_t r64 = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
    vb_cpu.r[30] = (uint32_t)((uint64_t)r64 >> 32);
    uint32_t lo = (uint32_t)r64;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    if (r64 == 0) psw |= VB_PSW_Z;
    if (r64 < 0) psw |= VB_PSW_S;
    if (r64 > 0x7FFFFFFF || r64 < -(int64_t)0x80000000) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return lo;
}

/* MULU: unsigned 32x32 -> 64; low word returned, high word into r30.
 * Z and S read the full 64-bit product, as in vb_mul -- S is bit 63, so a
 * product whose low word merely has bit 31 set is still positive. */
static inline uint32_t vb_mulu(uint32_t a, uint32_t b) {
    uint64_t r64 = (uint64_t)a * (uint64_t)b;
    vb_cpu.r[30] = (uint32_t)(r64 >> 32);
    uint32_t lo = (uint32_t)r64;
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    if (r64 == 0) psw |= VB_PSW_Z;
    if (r64 >> 63) psw |= VB_PSW_S;
    if (r64 > 0xFFFFFFFFu) psw |= VB_PSW_OV;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return lo;
}

/* DIV: signed quotient returned, remainder into r30. Caller guards b != 0.
 *
 * INT_MIN / -1 is the one case that must not reach the host divide: the
 * quotient does not fit in 32 bits, so x86 `idiv` raises and takes the process
 * down, while the V810 completes the instruction with INT_MIN and OV set. */
static inline uint32_t vb_div(uint32_t a, uint32_t b) {
    uint32_t psw = vb_cpu.sr[VB_SREG_PSW] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV);
    int32_t q;
    if (a == 0x80000000u && b == 0xFFFFFFFFu) {
        q = (int32_t)0x80000000;
        vb_cpu.r[30] = 0;
        psw |= VB_PSW_OV;
    } else {
        q = (int32_t)a / (int32_t)b;
        vb_cpu.r[30] = (uint32_t)((int32_t)a % (int32_t)b);
    }
    if (q == 0) psw |= VB_PSW_Z;
    if (q < 0) psw |= VB_PSW_S;
    vb_cpu.sr[VB_SREG_PSW] = psw;
    return (uint32_t)q;
}

/* DIVU: unsigned quotient returned, remainder into r30. Caller guards b != 0.
 * An unsigned divide cannot overflow, so OV is always cleared. */
static inline uint32_t vb_divu(uint32_t a, uint32_t b) {
    vb_cpu.r[30] = a % b;
    uint32_t q = a / b;
    vb_cpu.sr[VB_SREG_PSW] &= ~VB_PSW_OV;
    vb_cpu_set_flags_zs(q);
    return q;
}

#endif /* VBRECOMP_CPU_H */
