/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 */

#include "vbrecomp/cpu.h"
#include "vbrecomp/mem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

vb_cpu_state_t vb_cpu;

/* Pending tail jump; see cpu.h. */
uint32_t vb_recomp_tail_target;

/* Depth trace for the generated dispatcher: names the control-flow cycle that
 * is nesting a frame per iteration instead of running flat. */
void vb_recomp_deep(uint32_t addr, int depth) {
    static int shown;
    if (shown++ < 40) fprintf(stderr, "DEEP %3d -> sub_%08X\n", depth, addr);
}

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
 *   r30 = source word address       r27 = source bit offset      (0-31)
 *   r29 = destination word address  r26 = destination bit offset (0-31)
 *   r28 = length in BITS
 *
 * The addresses live in r29/r30 and the offsets in r26/r27 -- not the other way
 * round, which is what this originally had. Real ROM code settles it: Golf's
 * wrapper at sub_07E0E4D0 loads `r29 = r6; r26 = (r6 & 3) << 3` and
 * `r30 = r7; r27 = (r7 & 3) << 3`, pairing each address with the bit offset
 * derived from its own low two bits, and Ballface blits with `r27 = 0` (a
 * word-aligned source) and `r26 = r5` (a variable destination bit position, the
 * x of a glyph). With the operands swapped, every blit read and wrote near
 * address zero -- scribbling on the left framebuffer while the real destination
 * stayed empty, which is worse than the no-op this replaced.
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
    uint32_t src_addr = vb_cpu.r[30] & ~3u;
    uint32_t dst_addr = vb_cpu.r[29] & ~3u;
    uint32_t len      = vb_cpu.r[28];
    uint32_t src_bit  = vb_cpu.r[27] & 31u;
    uint32_t dst_bit  = vb_cpu.r[26] & 31u;

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

    vb_cpu.r[30] = src_addr;
    vb_cpu.r[29] = dst_addr;
    vb_cpu.r[28] = 0;
    vb_cpu.r[27] = src_bit;
    vb_cpu.r[26] = dst_bit;
}

/*
 * Unimplemented-call tracking.
 *
 * Three places in the generated code are holes the translation left behind: a
 * function the analyzer confirmed but never walked (emitted as a stub), a JAL
 * to a callee it never found, and an indirect jump whose target is not in the
 * dispatch table. All three are silent -- the game calls them, nothing happens,
 * and it runs on forever with state it never actually computed. That is what a
 * "boots but draws nothing" ROM usually looks like from the outside.
 *
 * Counting them turns that into a name: the address hit 400,000 times is the
 * function worth teaching the analyzer to find. The heartbeat prints the top
 * few (VBRECOMP_HEARTBEAT=1), alongside the hot reads.
 */
#define STUB_N 512
static uint32_t stub_addr[STUB_N];
static uint32_t stub_cnt[STUB_N];
static uint32_t stub_total;

void vb_recomp_stub(uint32_t addr) {
    uint32_t h = (addr * 2654435761u) % STUB_N;
    stub_total++;
    /* A jump to address 0 is always a bug, and the register file at that
     * instant is the whole question: which pointer was null, and what was it
     * read from. Print the first few with context rather than only a count. */
    /* Any target outside ROM is a lost jump, not just zero: Jack Bros lands
     * on 0x00030002 and Hyper Fighting on 0x0001CC80, both inside VIP VRAM,
     * where a register holding data has reached a jmp. Same question, same
     * answer needed. */
    /* Context for ANY unresolved target, not just out-of-ROM ones: an
     * in-ROM address that still misses the table is just as lost, and the
     * registers say who sent us there. */
    if (getenv("VBRECOMP_HEARTBEAT")) {
        static int z = 0;
        if (z++ < 8)
            fprintf(stderr,
                "LOST JUMP -> %08X (#%d) r6=%08X r7=%08X r10=%08X r29=%08X r31=%08X "
                "[r29+12]=%08X\n",
                addr, z, vb_cpu.r[6], vb_cpu.r[7], vb_cpu.r[10], vb_cpu.r[29],
                vb_cpu.r[31], vb_mem_read32(vb_cpu.r[29] + 12));
    }
    for (uint32_t i = 0; i < STUB_N; i++) {
        uint32_t j = (h + i) % STUB_N;
        if (stub_cnt[j] == 0) { stub_addr[j] = addr; stub_cnt[j] = 1; return; }
        if (stub_addr[j] == addr) { stub_cnt[j]++; return; }
    }
    /* ponytail: table full -- the counts still rank the first 512 addresses,
     * which is far more than any one game has holes. */
}

/*
 * Execution trace: the last functions entered, as a ring.
 *
 * When a recompiled boot chain simply ends, nothing says WHERE. The hot-read
 * list gives an address, VBRECOMP_WATCH gives its reader and VBRECOMP_PEEK
 * gives a value, but none of them answer "what ran last" -- which is the only
 * question that matters for a game whose entry returns having drawn nothing.
 * This is the backtrace the generated code never had.
 */
#define TRACE_N 64
static uint32_t trace_ring[TRACE_N];
static uint32_t trace_pos;

/* Coarse program counter: the generated code stores the address of each
 * basic block it enters. The trace ring records FUNCTION entries, which
 * says nothing about a loop that spins inside one function without calling
 * anything -- Hyper Fighting (Beta) hammers 4 KB of CHR 36,000 times a
 * frame with no repeated memory read, so neither the ring nor the hot-read
 * list can place it. One store per block is cheap and does. */
uint32_t vb_recomp_pc;
uint32_t vb_recomp_entry;
int vb_recomp_irq_entry;
/* Lowest call depth an unwind may reach. The interrupt dispatcher raises it to
 * the interrupted code's depth while a handler runs: the frames below belong
 * to the code that was interrupted, and a transfer inside the handler must not
 * unwind through them. Panic Bomber's timer handler did, never reached its
 * RETI, and left the game running with EP set, waiting on a VIP interrupt that
 * EP blocks. */
int vb_recomp_frame_floor;

/* VBRECOMP_COUNT_FUNC=<hex>: count entries to one function. The interrupt
 * dispatcher reports it per handler call, which is how you test a claim like
 * "the handler re-runs 42 times per interrupt" instead of inferring it from
 * an SP delta that happens to divide by four. */
uint32_t vb_recomp_func_hits;
uint32_t vb_recomp_func_total;   /* same count, never reset: for the heartbeat */
static uint32_t vb_recomp_func_watch = 0xFFFFFFFFu;

void vb_recomp_trace(uint32_t addr) {
    trace_ring[trace_pos++ % TRACE_N] = addr;
    if (vb_recomp_func_watch == 0xFFFFFFFFu) {
        const char *e = getenv("VBRECOMP_COUNT_FUNC");
        vb_recomp_func_watch = e ? (uint32_t)strtoul(e, NULL, 16) : 0;
    }
    if (vb_recomp_func_watch && addr == vb_recomp_func_watch) { vb_recomp_func_hits++; vb_recomp_func_total++; }
}

void vb_recomp_dump_trace(void) {
    uint32_t n = trace_pos < TRACE_N ? trace_pos : TRACE_N;
    fprintf(stderr, "  last %u functions entered (most recent last):\n", n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (trace_pos - n + i) % TRACE_N;
        fprintf(stderr, "    %2u sub_%08X\n", i, trace_ring[idx]);
    }
}

void vb_recomp_dump_stubs(void) {
    if (!stub_total) return;
    fprintf(stderr, "  unimplemented calls: %u total\n", stub_total);
    for (int k = 0; k < 6; k++) {
        int best = -1; uint32_t bc = 0;
        for (uint32_t i = 0; i < STUB_N; i++)
            if (stub_cnt[i] > bc) { bc = stub_cnt[i]; best = (int)i; }
        if (best < 0) break;
        fprintf(stderr, "    sub_%08X x%u\n", stub_addr[best], stub_cnt[best]);
        stub_cnt[best] = 0;
    }
    memset(stub_cnt, 0, sizeof(stub_cnt));
    stub_total = 0;
}
