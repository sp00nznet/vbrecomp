/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 */

#include "vbrecomp/cpu.h"
#include "vbrecomp/mem.h"
#include <string.h>

vb_cpu_state_t vb_cpu;

void vb_cpu_init(void) {
    memset(&vb_cpu, 0, sizeof(vb_cpu));
    /* r0 is always zero - enforced by convention */
    vb_cpu.r[0] = 0;
    /* PIR: processor ID for V810 */
    vb_cpu.sr[VB_SREG_PIR] = 0x00005346;
    /* Initial PSW: interrupts disabled, NMI pending */
    vb_cpu.sr[VB_SREG_PSW] = VB_PSW_NP | VB_PSW_ID;
    /* PC starts at reset vector */
    vb_cpu.pc = 0xFFFFFFF0;
}

/*
 * Bit string operations (opcode 0x1F).
 *
 * The V810's block-copy instruction, and the reason a VB game can blit a font
 * or a decompressed tile into VRAM at an arbitrary bit offset. Operands live in
 * fixed registers rather than in the encoding:
 *
 *   r26 = source word address     r30 = source bit offset   (0-31)
 *   r27 = destination word address r29 = destination bit offset (0-31)
 *   r28 = length in BITS
 *
 * The instruction is restartable, so it leaves those registers pointing at
 * whatever it did not finish: addresses advance a word at a time, the offsets
 * hold the next bit, and the length ends at zero. Games rely on that -- the
 * usual idiom runs the same movbsu again to continue a longer copy.
 *
 * ponytail: one bit at a time. A whole-word fast path is worth having if a
 * profile ever shows this dominating, but the bit loop is what makes the
 * arbitrary-offset case obviously right, and correctness came first here.
 */
void vb_bitstring(int op) {
    uint32_t src_addr = vb_cpu.r[26] & ~3u;
    uint32_t dst_addr = vb_cpu.r[27] & ~3u;
    uint32_t len      = vb_cpu.r[28];
    uint32_t dst_bit  = vb_cpu.r[29] & 31u;
    uint32_t src_bit  = vb_cpu.r[30] & 31u;

    while (len > 0) {
        uint32_t s = (vb_mem_read32(src_addr) >> src_bit) & 1u;
        uint32_t d = (vb_mem_read32(dst_addr) >> dst_bit) & 1u;
        uint32_t r;
        switch (op) {
        case VB_BSU_OR:     r = s | d;      break;
        case VB_BSU_AND:    r = s & d;      break;
        case VB_BSU_XOR:    r = s ^ d;      break;
        case VB_BSU_MOV:    r = s;          break;
        case VB_BSU_ORN:    r = (!s) | d;   break;
        case VB_BSU_ANDN:   r = (!s) & d;   break;
        case VB_BSU_XORN:   r = !(s ^ d);   break;
        case VB_BSU_NOT:    r = !s;         break;
        default:            return;  /* search ops (SCH*BSU) are separate */
        }
        uint32_t w = vb_mem_read32(dst_addr);
        w = (w & ~(1u << dst_bit)) | ((r & 1u) << dst_bit);
        vb_mem_write32(dst_addr, w);

        if (++src_bit == 32) { src_bit = 0; src_addr += 4; }
        if (++dst_bit == 32) { dst_bit = 0; dst_addr += 4; }
        len--;
    }

    vb_cpu.r[26] = src_addr;
    vb_cpu.r[27] = dst_addr;
    vb_cpu.r[28] = 0;
    vb_cpu.r[29] = dst_bit;
    vb_cpu.r[30] = src_bit;
}
