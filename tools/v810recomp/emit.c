/*
 * v810recomp - C code emitter
 * SPDX-License-Identifier: MIT
 *
 * Translates analyzed V810 functions into C code that runs on vbrecomp.
 */

#include "v810recomp.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef struct {
    uint32_t *addrs;
    int count;
    int cap;
} label_set_t;

static void label_set_add(label_set_t *ls, uint32_t addr);
static bool label_set_has(label_set_t *ls, uint32_t addr);

/* Condition code expressions for Bcond/SETF */
static const char *cond_expr[16] = {
    /* 0x0 BV  */ "(vb_cpu.sr[5] & VB_PSW_OV)",
    /* 0x1 BL  */ "(vb_cpu.sr[5] & VB_PSW_CY)",
    /* 0x2 BZ  */ "(vb_cpu.sr[5] & VB_PSW_Z)",
    /* 0x3 BNH */ "((vb_cpu.sr[5] & VB_PSW_CY) || (vb_cpu.sr[5] & VB_PSW_Z))",
    /* 0x4 BN  */ "(vb_cpu.sr[5] & VB_PSW_S)",
    /* 0x5 BR  */ "1",
    /* 0x6 BLT */ "((!!(vb_cpu.sr[5] & VB_PSW_OV)) ^ (!!(vb_cpu.sr[5] & VB_PSW_S)))",
    /* 0x7 BLE */ "(((!!(vb_cpu.sr[5] & VB_PSW_OV)) ^ (!!(vb_cpu.sr[5] & VB_PSW_S))) || (vb_cpu.sr[5] & VB_PSW_Z))",
    /* 0x8 BNV */ "(!(vb_cpu.sr[5] & VB_PSW_OV))",
    /* 0x9 BNL */ "(!(vb_cpu.sr[5] & VB_PSW_CY))",
    /* 0xA BNZ */ "(!(vb_cpu.sr[5] & VB_PSW_Z))",
    /* 0xB BH  */ "(!(vb_cpu.sr[5] & VB_PSW_CY) && !(vb_cpu.sr[5] & VB_PSW_Z))",
    /* 0xC BP  */ "(!(vb_cpu.sr[5] & VB_PSW_S))",
    /* 0xD NOP */ "0",
    /* 0xE BGE */ "((!!(vb_cpu.sr[5] & VB_PSW_OV)) == (!!(vb_cpu.sr[5] & VB_PSW_S)))",
    /* 0xF BGT */ "(((!!(vb_cpu.sr[5] & VB_PSW_OV)) == (!!(vb_cpu.sr[5] & VB_PSW_S))) && !(vb_cpu.sr[5] & VB_PSW_Z))",
};

/* Emit flag-setting code for arithmetic operations */
/* qsort comparator for the runtime dispatch table (ascending by address). */
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void emit_flags_add(FILE *out, const char *a, const char *b, const char *result) {
    /* result == NULL means an r0 destination: the sum is discarded but the
     * flags are not. emit_flags_sub has always taken NULL (that is how CMP is
     * emitted); this one did not, and printed "(null)" into the C. */
    if (result) {
        fprintf(out, "    %s = vb_add(%s, %s);\n", result, a, b);
    } else {
        fprintf(out, "    { uint32_t _d = vb_add(%s, %s); (void)_d; }\n", a, b);
    }
}

static void emit_flags_sub(FILE *out, const char *a, const char *b, const char *result) {
    /* a - b, result goes into 'result' (or NULL for CMP) */
    if (result) {
        fprintf(out, "    %s = vb_sub(%s, %s);\n", result, a, b);
    } else {
        fprintf(out, "    vb_cmp(%s, %s);\n", a, b);
    }
}

static void emit_flags_zs(FILE *out, const char *result) {
    /* Logical-op flags: Z/S from result, OV cleared. */
    fprintf(out, "    vb_setf_logic(%s);\n", result);
}

static void emit_flags_shift(FILE *out, const char *reg, const char *shift_expr, const char *op, bool arithmetic) {
    const char *fn = (strcmp(op, "<<") == 0) ? "vb_shl" : (arithmetic ? "vb_sar" : "vb_shr");
    fprintf(out, "    %s = %s(%s, %s);\n", reg, fn, reg, shift_expr);
}

/* reg accessor macros to keep output readable */
#define R(n)  "vb_cpu.r[" #n "]"

static void emit_reg(FILE *out, int reg) {
    fprintf(out, "vb_cpu.r[%d]", reg);
}

/* Check if a target address is a known function */
/* Only CONFIRMED functions count here. Callers use this to decide whether to
 * emit a direct `vb_func_<addr>()` call, and an unconfirmed entry is never
 * declared, never defined and gets no stub -- so matching one produces a call
 * to a symbol that does not exist, and the game fails to link. */
static int find_func_by_addr(v810_ctx_t *ctx, uint32_t addr) {
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (ctx->funcs[i].addr == addr && ctx->funcs[i].confirmed) return i;
    }
    return -1;
}

/* Emit a single instruction as C code */
/* Look up a jump table for a JMP address. Returns index or -1. */
static int lookup_jump_table(v810_ctx_t *ctx, uint32_t jmp_addr) {
    for (int i = 0; i < ctx->num_jump_tables; i++) {
        if (ctx->jump_tables[i].jmp_addr == jmp_addr) return i;
    }
    return -1;
}

/* Look up a resolved indirect jump target. Returns 0 if not found. */
static uint32_t lookup_resolved_jump(v810_ctx_t *ctx, uint32_t from_addr) {
    for (int i = 0; i < ctx->num_resolved; i++) {
        if (ctx->resolved_jumps[i].from_addr == from_addr)
            return ctx->resolved_jumps[i].to_addr;
    }
    return 0;
}

/* Wholesale loads of the STACK POINTER from memory in the function being
 * emitted. Ordinary code only ADDS to r3; replacing it outright is either a
 * context SWITCH (loading another task's stack) or the RESTORE of a stack this
 * same function saved. They are told apart by whether the function also stores
 * r3 to that exact slot: a save/restore pair has a matching `st.w r3, off[rN]`,
 * a switch does not.
 *
 * It matters because after a switch, `jmp [r31]` resumes a DIFFERENT stack's
 * continuation instead of returning to our caller. Jack Bros' scheduler
 * (sub_07F020B0) restores a task's registers, pops its resume PC into r31 and
 * jumps -- emitted as a plain return, its cooperative tasks are created and
 * never run. Rare enough to key on: 128 such loads in the whole corpus, none
 * at all in most games. */
#define MAX_SP_LOADS 24
static struct { uint32_t addr; bool is_switch; } g_sp_loads[MAX_SP_LOADS];
static int g_num_sp_loads;

/* Was the most recent stack-pointer load before `addr` a context switch? */
static bool after_stack_switch(uint32_t addr) {
    bool sw = false;
    uint32_t best = 0;
    for (int i = 0; i < g_num_sp_loads; i++) {
        if (g_sp_loads[i].addr < addr && g_sp_loads[i].addr >= best) {
            best = g_sp_loads[i].addr;
            sw = g_sp_loads[i].is_switch;
        }
    }
    return sw;
}
static void emit_insn(v810_ctx_t *ctx, FILE *out, const v810_insn_t *insn, label_set_t *emit_labels) {
    char r1[32], r2[32];
    snprintf(r1, sizeof(r1), "vb_cpu.r[%d]", insn->reg1);
    snprintf(r2, sizeof(r2), "vb_cpu.r[%d]", insn->reg2);

    /* Comment with original disassembly */
    char disasm[128];
    v810_disasm(insn, disasm, sizeof(disasm));
    fprintf(out, "    /* %08X: %s */\n", insn->addr, disasm);

    switch (insn->opcode) {
    /* --- Format I: register-register --- */
    case 0x00: /* MOV reg1, reg2 */
        if (insn->reg2 != 0) fprintf(out, "    %s = %s;\n", r2, r1);
        break;

    case 0x01: /* ADD reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_add(out, r2, r1, r2);
        } else {  /* r0 destination: result discarded, flags still set */
            emit_flags_add(out, r2, r1, NULL);
        }
        break;

    case 0x02: /* SUB reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_sub(out, r2, r1, r2);
        } else {  /* r0 destination: result discarded, flags still set */
            emit_flags_sub(out, r2, r1, NULL);
        }
        break;

    case 0x03: /* CMP reg1, reg2 */
        emit_flags_sub(out, r2, r1, NULL);
        break;

    case 0x04: /* SHL reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_shift(out, r2, r1, "<<", false);
        }
        break;

    case 0x05: /* SHR reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_shift(out, r2, r1, ">>", false);
        }
        break;

    case 0x06: /* JMP [reg1] */
        /* jmp [r31] is a return UNLESS we resolved r31 to a constant target
         * (reset-vector idiom: load r31, jmp to entry). A resolved jump takes
         * precedence so the computed-jump path below emits the real dispatch. */
        if (insn->reg1 == 31 && after_stack_switch(insn->addr)) {
            fprintf(out, "    /* jmp [r31] after a stack switch: a context resume, not a return */\n");
            fprintf(out, "    VB_JUMP(vb_cpu.r[31] & ~1u);\n");
        } else if (insn->reg1 == 31 && !lookup_resolved_jump(ctx, insn->addr)) {
            fprintf(out, "    VB_RET(); /* jmp [r31] */\n");
        } else {
            /* Check for jump table first */
            int jt_idx = lookup_jump_table(ctx, insn->addr);
            if (jt_idx >= 0) {
                fprintf(out, "    /* Jump table dispatch (%d entries) */\n",
                        ctx->jump_tables[jt_idx].num_entries);
                fprintf(out, "    switch (%s) {\n", r1);
                /* Use raw addresses for case values, mapped for function names */
                for (int e = 0; e < ctx->jump_tables[jt_idx].num_entries; e++) {
                    uint32_t raw = ctx->jump_tables[jt_idx].raw_targets[e];
                    uint32_t mapped = ctx->jump_tables[jt_idx].targets[e];
                    bool dup = false;
                    for (int p = 0; p < e; p++) {
                        if (ctx->jump_tables[jt_idx].raw_targets[p] == raw) { dup = true; break; }
                    }
                    if (!dup) {
                        fprintf(out, "    case 0x%08Xu: VB_JUMP(0x%08Xu);\n", raw, mapped);
                    }
                }
                fprintf(out, "    default: vb_recomp_stub(%s); return;\n", r1);
                fprintf(out, "    }\n");
                /* A table dispatch is a JUMP, not a call: every case transfers.
                 * Calling the handler instead nests a frame per iteration, and
                 * the game main loop it sits in runs forever -- Mario Clash
                 * dies partway through a run that way. */
                break;
            }
            uint32_t resolved = lookup_resolved_jump(ctx, insn->addr);
            if (resolved) {
                /* Check if target is another function (tail call) or intraprocedural */
                if (label_set_has(emit_labels, resolved)) {
                    /* Target is a guaranteed-defined label in THIS function
                     * (intraprocedural computed jump). */
                    fprintf(out, "    goto label_%08X; /* resolved jmp [r%d] */\n",
                            resolved, insn->reg1);
                } else {
                    /* Cross-function computed target: dispatch through the
                     * runtime table by address rather than emitting a direct
                     * symbol reference. A direct `vb_func_X()` would fail to
                     * link when X is a phantom entry that was never emitted --
                     * e.g. a jump into the MIDDLE of another function (its head
                     * is elsewhere, so no standalone body exists), or an
                     * unconfirmed callee that got no stub. The table normalizes
                     * ROM mirrors and no-ops on an unknown address, while still
                     * reaching real heads (the reset-vector entry included). */
                    /* Dispatch on the register's ACTUAL value, not the constant
                     * analysis resolved it to. A `jmp [rN]` is a runtime
                     * dispatch and rN takes many values; the static resolution
                     * finds one of them, which is what makes the target a
                     * discoverable function -- but hardcoding it sends every
                     * dispatch to the same place. StarFox's sub_07FE64E4 is
                     * `add 4, r31; jmp [r10]`, a vtable-style dispatcher, and
                     * pinning it to 0x07FE0300 made the game run the same two
                     * states forever and then fall out of its boot chain. */
                    fprintf(out, "    vb_cpu.pc = %s & 0xFFFFFFFEu;\n", r1);
                    fprintf(out, "    VB_JUMP(vb_cpu.pc); /* jmp [r%d], resolved target 0x%08X */\n",
                            insn->reg1, resolved);
                }
            } else {
                /* Unresolved at compile time: dispatch through the runtime
                 * function table by the register's value (tail call). */
                fprintf(out, "    vb_cpu.pc = %s & 0xFFFFFFFE;\n", r1);
                fprintf(out, "    VB_JUMP(vb_cpu.pc); /* indirect jump [%s] */\n", r1);
            }
        }
        break;

    case 0x07: /* SAR reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_shift(out, r2, r1, ">>", true);
        }
        break;

    case 0x08: /* MUL reg1, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = vb_mul(%s, %s);\n", r2, r2, r1);
        } else {
            /* Writing r0 would break the always-zero invariant every
             * later `movhi imm, r0, rN` depends on -- but the helper
             * still has to run for its flags and its r30 half. */
            fprintf(out, "    { uint32_t _d = vb_mul(0u, %s); (void)_d; }\n", r1);
        }
        break;

    case 0x09: /* DIV reg1, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    if (%s != 0) %s = vb_div(%s, %s);\n", r1, r2, r2, r1);
        } else {  /* keep r0 zero; see MUL */
            fprintf(out, "    if (%s != 0) { uint32_t _d = vb_div(0u, %s); (void)_d; }\n", r1, r1);
        }
        break;

    case 0x0A: /* MULU reg1, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = vb_mulu(%s, %s);\n", r2, r2, r1);
        } else {
            /* Writing r0 would break the always-zero invariant every
             * later `movhi imm, r0, rN` depends on -- but the helper
             * still has to run for its flags and its r30 half. */
            fprintf(out, "    { uint32_t _d = vb_mulu(0u, %s); (void)_d; }\n", r1);
        }
        break;

    case 0x0B: /* DIVU reg1, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    if (%s != 0) %s = vb_divu(%s, %s);\n", r1, r2, r2, r1);
        } else {  /* keep r0 zero; see MUL */
            fprintf(out, "    if (%s != 0) { uint32_t _d = vb_divu(0u, %s); (void)_d; }\n", r1, r1);
        }
        break;

    case 0x0C: /* OR */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s | %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
        } else {  /* r0 destination still sets Z/S -- see ANDI */
            char e[80];
            snprintf(e, sizeof(e), "(uint32_t)0 | %s", r1);
            emit_flags_zs(out, e);
        }
        break;

    case 0x0D: /* AND */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s & %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
        } else {  /* r0 destination still sets Z/S -- see ANDI */
            char e[80];
            snprintf(e, sizeof(e), "(uint32_t)0 & %s", r1);
            emit_flags_zs(out, e);
        }
        break;

    case 0x0E: /* XOR */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s ^ %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
        } else {  /* r0 destination still sets Z/S -- see ANDI */
            char e[80];
            snprintf(e, sizeof(e), "(uint32_t)0 ^ %s", r1);
            emit_flags_zs(out, e);
        }
        break;

    case 0x0F: /* NOT */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = ~%s;\n", r2, r1);
            emit_flags_zs(out, r2);
        }
        break;

    /* --- Format II: immediate/special --- */
    case 0x10: /* MOV imm5, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = 0x%08X;\n", r2, (uint32_t)insn->imm);
        }
        break;

    case 0x11: /* ADD imm5, reg2 */ {
        char imm_str[32];
        snprintf(imm_str, sizeof(imm_str), "0x%08X", (uint32_t)insn->imm);
        if (insn->reg2 != 0) {
            emit_flags_add(out, r2, imm_str, r2);
        }
        break;
    }

    case 0x12: /* SETF cond, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s ? 1 : 0;\n", r2, cond_expr[insn->reg1 & 0xF]);
        }
        break;

    case 0x13: /* CMP imm5, reg2 */ {
        char imm_str[32];
        snprintf(imm_str, sizeof(imm_str), "0x%08X", (uint32_t)insn->imm);
        emit_flags_sub(out, r2, imm_str, NULL);
        break;
    }

    case 0x14: /* SHL imm5, reg2 */
        if (insn->reg2 != 0) {
            char imm_str[16];
            snprintf(imm_str, sizeof(imm_str), "%u", insn->reg1);
            emit_flags_shift(out, r2, imm_str, "<<", false);
        }
        break;

    case 0x15: /* SHR imm5, reg2 */
        if (insn->reg2 != 0) {
            char imm_str[16];
            snprintf(imm_str, sizeof(imm_str), "%u", insn->reg1);
            emit_flags_shift(out, r2, imm_str, ">>", false);
        }
        break;

    case 0x16: /* CLI */
        fprintf(out, "    vb_cpu.sr[5] &= ~VB_PSW_ID;\n");
        break;

    case 0x17: /* SAR imm5, reg2 */
        if (insn->reg2 != 0) {
            char imm_str[16];
            snprintf(imm_str, sizeof(imm_str), "%u", insn->reg1);
            emit_flags_shift(out, r2, imm_str, ">>", true);
        }
        break;

    case 0x18: /* TRAP */
        fprintf(out, "    /* TRAP %d */\n", insn->reg1);
        fprintf(out, "    vb_cpu.sr[0] = 0x%08X; /* EIPC = PC+2 */\n", insn->addr + 2);
        fprintf(out, "    vb_cpu.sr[1] = vb_cpu.sr[5]; /* EIPSW */\n");
        fprintf(out, "    vb_cpu.sr[5] |= VB_PSW_EP | VB_PSW_ID;\n");
        break;

    case 0x19: /* RETI */
        fprintf(out, "    if (vb_cpu.sr[5] & VB_PSW_NP) {\n");
        fprintf(out, "      vb_cpu.pc = vb_cpu.sr[2]; vb_cpu.sr[5] = vb_cpu.sr[3];\n");
        fprintf(out, "    } else {\n");
        fprintf(out, "      vb_cpu.pc = vb_cpu.sr[0]; vb_cpu.sr[5] = vb_cpu.sr[1];\n");
        fprintf(out, "    }\n");
        fprintf(out, "    return;\n");
        break;

    case 0x1A: /* HALT */
        fprintf(out, "    /* HALT - wait for interrupt */\n");
        fprintf(out, "    vb_interrupt_check();\n");
        break;

    case 0x1C: /* LDSR reg2, regID */
        fprintf(out, "    vb_cpu.sr[%d] = %s;\n", insn->reg1, r2);
        break;

    case 0x1D: /* STSR regID, reg2 */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = vb_cpu.sr[%d];\n", r2, insn->reg1);
        }
        break;

    case 0x1E: /* SEI */
        fprintf(out, "    vb_cpu.sr[5] |= VB_PSW_ID;\n");
        break;

    case 0x1F: /* Bitstring ops: the sub-opcode sits in the imm5 field */
        if (insn->reg1 >= 0x08) {
            fprintf(out, "    vb_bitstring(0x%02X);\n", insn->reg1);
        } else {
            /* SCH0BSU/SCH1BSU and friends search a bit string rather than
             * transferring one. No ROM in the corpus uses one, so they stay
             * unimplemented rather than guessed at -- and stay visible as a
             * comment if one ever turns up. */
            fprintf(out, "    /* TODO: bitstring search op %d */\n", insn->reg1);
        }
        break;

    /* --- Format III: Bcond --- */
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27: {
        uint32_t target = insn->addr + insn->imm;
        if (insn->cond == 0x0D) {
            /* NOP */
            fprintf(out, "    /* nop */\n");
        } else {
            /* Check label set first (intraprocedural takes priority) */
            if (emit_labels && label_set_has(emit_labels, target)) {
                /* Intraprocedural branch */
                if (insn->cond == 0x05) {
                    fprintf(out, "    goto label_%08X;\n", target);
                } else {
                    fprintf(out, "    if (%s) goto label_%08X;\n",
                            cond_expr[insn->cond], target);
                }
            } else if (find_func_by_addr(ctx, target) >= 0) {
                /* Branch to a known function = conditional tail call */
                if (insn->cond == 0x05) {
                    fprintf(out, "    VB_JUMP(0x%08Xu);\n", target);
                } else {
                    fprintf(out, "    if (%s) { VB_JUMP(0x%08Xu); }\n",
                            cond_expr[insn->cond], target);
                }
            } else {
                /* External branch to unknown code */
                if (insn->cond == 0x05) {
                    fprintf(out, "    VB_JUMP(0x%08Xu); /* out of function */\n", target);
                } else {
                    fprintf(out, "    if (%s) { VB_JUMP(0x%08Xu); }\n",
                            cond_expr[insn->cond], target);
                }
            }
        }
        break;
    }

    /* --- Format IV: JR / JAL --- */
    case 0x2A: { /* JR */
        uint32_t target = insn->addr + insn->imm;
        if (emit_labels && label_set_has(emit_labels, target)) {
            fprintf(out, "    goto label_%08X;\n", target);
        } else {
            /* A JR out of the function is a TRANSFER. Park it for the caller's
             * drain rather than calling it (unbounded recursion once a main-loop
             * cycle runs through here) or returning (drops the loop entirely). */
            fprintf(out, "    VB_JUMP(0x%08Xu); /* jr */\n", target);
        }
        break;
    }

    case 0x2B: { /* JAL */
        uint32_t target = insn->addr + insn->imm;
        uint32_t skip = v810_get_skip_bytes(ctx, target);
        fprintf(out, "    vb_cpu.r[31] = 0x%08X;\n", insn->addr + 4 + skip);
        /* `jal <next instruction>` is not a call: it is the compiler's
         * PC-capture idiom for a long call --
         *     jal  .+4        ; r31 = address of the next insn
         *     add  4, r31     ; r31 = the real return address
         *     jmp  [rN]       ; tail-jump to the far target
         * Emitting it as a call runs the following instructions twice (once
         * inside the callee, once inline after it returns) with r31 already
         * advanced, which is how StarFox lost control: sub_07FE0300 ran a
         * second time and returned through a corrupted link register. Setting
         * r31 and falling through is the whole instruction. */
        if (target == insn->addr + 4 && skip == 0) break;
        /* Normalize the target the same way declarations/stubs do. An
         * in-ROM target is a direct call; an out-of-ROM one (a mirror or a
         * garbage target from data decoded as JAL) has no emitted function,
         * so dispatch it at run time (no-op on miss) to stay linkable. */
        uint32_t nt = target;
        nt = v810_normalize_rom_addr(ctx, nt);
        if (nt >= ctx->rom_base && nt < ROM_REGION_END) {
            fprintf(out, "    VB_CALL(vb_func_%08X, 0x%08Xu);\n", nt, nt);
        } else {
            fprintf(out, "    vb_recomp_call(0x%08Xu); vb_recomp_drain();\n", target);
        }
        if (skip > 0) {
            fprintf(out, "    /* skip %u bytes of inline data after JAL */\n", skip);
        }
        break;
    }

    /* --- Format V: immediate 16-bit --- */
    case 0x28: /* MOVEA */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s + (int32_t)(int16_t)0x%04X;\n", r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x29: /* ADDI */ {
        char imm_str[32];
        snprintf(imm_str, sizeof(imm_str), "(int32_t)(int16_t)0x%04X", (uint16_t)insn->imm);
        if (insn->reg2 != 0) {
            emit_flags_add(out, r1, imm_str, r2);
        }
        break;
    }

    case 0x2C: /* ORI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s | 0x%04X;\n", r2, r1, (uint16_t)insn->imm);
            emit_flags_zs(out, r2);
        } else {  /* r0 destination still sets Z/S -- see ANDI */
            char e[80];
            snprintf(e, sizeof(e), "%s | 0x%04X", r1, (uint16_t)insn->imm);
            emit_flags_zs(out, e);
        }
        break;

    case 0x2D: /* ANDI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s & 0x%04X;\n", r2, r1, (uint16_t)insn->imm);
            emit_flags_zs(out, r2);
        } else {
            /* `andi imm, reg, r0` is the V810 TEST idiom: the result is thrown
             * away but Z/S are still set, and the very next branch depends on
             * them. Emitting nothing at all left that branch reading stale
             * flags -- which is how 3-D Tetris's VIP handler tested FRAMESTART
             * (`andi 0x0010, r11, r0; bz`) and always took the wrong arm. */
            char e[80];
            snprintf(e, sizeof(e), "%s & 0x%04X", r1, (uint16_t)insn->imm);
            emit_flags_zs(out, e);
        }
        break;

    case 0x2E: /* XORI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s ^ 0x%04X;\n", r2, r1, (uint16_t)insn->imm);
            emit_flags_zs(out, r2);
        } else {  /* r0 destination still sets Z/S -- see ANDI */
            char e[80];
            snprintf(e, sizeof(e), "%s ^ 0x%04X", r1, (uint16_t)insn->imm);
            emit_flags_zs(out, e);
        }
        break;

    case 0x2F: /* MOVHI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s + 0x%08X;\n", r2, r1, (uint32_t)((uint16_t)insn->imm) << 16);
        }
        break;

    /* --- Format VI: load/store --- */
    case 0x30: /* LD.B */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = (uint32_t)(int32_t)(int8_t)vb_mem_read8(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x31: /* LD.H */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = (uint32_t)(int32_t)(int16_t)vb_mem_read16(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x33: /* LD.W */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = vb_mem_read32(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x34: /* ST.B */
        fprintf(out, "    vb_mem_write8(%s + (int32_t)(int16_t)0x%04X, (uint8_t)%s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x35: /* ST.H */
        fprintf(out, "    vb_mem_write16(%s + (int32_t)(int16_t)0x%04X, (uint16_t)%s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x37: /* ST.W */
        fprintf(out, "    vb_mem_write32(%s + (int32_t)(int16_t)0x%04X, %s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x38: /* IN.B (same as LD.B on VB) */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = (uint32_t)vb_mem_read8(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x39: /* IN.H */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = (uint32_t)vb_mem_read16(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x3B: /* IN.W */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = vb_mem_read32(%s + (int32_t)(int16_t)0x%04X);\n",
                    r2, r1, (uint16_t)insn->imm);
        }
        break;

    case 0x3C: /* OUT.B */
        fprintf(out, "    vb_mem_write8(%s + (int32_t)(int16_t)0x%04X, (uint8_t)%s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x3D: /* OUT.H */
        fprintf(out, "    vb_mem_write16(%s + (int32_t)(int16_t)0x%04X, (uint16_t)%s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x3F: /* OUT.W */
        fprintf(out, "    vb_mem_write32(%s + (int32_t)(int16_t)0x%04X, %s);\n",
                r1, (uint16_t)insn->imm, r2);
        break;

    case 0x3A: /* CAXI */
        fprintf(out, "    /* CAXI - compare and exchange */\n");
        fprintf(out, "    { uint32_t _addr = %s + (int32_t)(int16_t)0x%04X;\n", r1, (uint16_t)insn->imm);
        fprintf(out, "      uint32_t _tmp = vb_mem_read32(_addr);\n");
        emit_flags_sub(out, r2, "_tmp", NULL);
        fprintf(out, "      if (%s == _tmp) vb_mem_write32(_addr, vb_cpu.r[30]);\n", r2);
        fprintf(out, "      %s = _tmp; }\n", r2);
        break;

    /* --- Format VII: subopcode --- */
    case 0x3E:
        switch (insn->subop) {
        case 0x00: /* CMPF.S */
            fprintf(out, "    { float _fa, _fb; memcpy(&_fa, &%s, 4); memcpy(&_fb, &%s, 4);\n", r2, r1);
            fprintf(out, "      uint32_t _psw = vb_cpu.sr[5] & ~(VB_PSW_Z|VB_PSW_S|VB_PSW_OV|VB_PSW_CY);\n");
            fprintf(out, "      if (_fa == _fb) _psw |= VB_PSW_Z;\n");
            fprintf(out, "      if (_fa < _fb) _psw |= VB_PSW_S | VB_PSW_CY;\n");
            fprintf(out, "      vb_cpu.sr[5] = _psw; }\n");
            break;
        case 0x02: /* CVT.WS */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _f = (float)(int32_t)%s; memcpy(&%s, &_f, 4); }\n", r1, r2);
            }
            break;
        case 0x03: /* CVT.SW */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _f; memcpy(&_f, &%s, 4); %s = (uint32_t)(int32_t)roundf(_f); }\n", r1, r2);
            }
            break;
        case 0x04: /* ADDF.S */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _fa, _fb; memcpy(&_fa, &%s, 4); memcpy(&_fb, &%s, 4);\n", r2, r1);
                fprintf(out, "      _fa += _fb; memcpy(&%s, &_fa, 4); }\n", r2);
            }
            break;
        case 0x05: /* SUBF.S */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _fa, _fb; memcpy(&_fa, &%s, 4); memcpy(&_fb, &%s, 4);\n", r2, r1);
                fprintf(out, "      _fa -= _fb; memcpy(&%s, &_fa, 4); }\n", r2);
            }
            break;
        case 0x06: /* MULF.S */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _fa, _fb; memcpy(&_fa, &%s, 4); memcpy(&_fb, &%s, 4);\n", r2, r1);
                fprintf(out, "      _fa *= _fb; memcpy(&%s, &_fa, 4); }\n", r2);
            }
            break;
        case 0x07: /* DIVF.S */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _fa, _fb; memcpy(&_fa, &%s, 4); memcpy(&_fb, &%s, 4);\n", r2, r1);
                fprintf(out, "      _fa /= _fb; memcpy(&%s, &_fa, 4); }\n", r2);
            }
            break;
        case 0x08: /* XB - exchange bytes in lower halfword */
            if (insn->reg2 != 0) {
                fprintf(out, "    %s = (%s & 0xFFFF0000) | ((%s >> 8) & 0xFF) | ((%s & 0xFF) << 8);\n",
                        r2, r2, r2, r2);
            }
            break;
        case 0x09: /* XH - exchange halfwords */
            if (insn->reg2 != 0) {
                fprintf(out, "    %s = (%s >> 16) | (%s << 16);\n", r2, r2, r2);
            }
            break;
        case 0x0A: /* REV - bit reverse */
            if (insn->reg2 != 0) {
                fprintf(out, "    { uint32_t _v = %s;\n", r1);
                fprintf(out, "      _v = ((_v >> 1) & 0x55555555) | ((_v & 0x55555555) << 1);\n");
                fprintf(out, "      _v = ((_v >> 2) & 0x33333333) | ((_v & 0x33333333) << 2);\n");
                fprintf(out, "      _v = ((_v >> 4) & 0x0F0F0F0F) | ((_v & 0x0F0F0F0F) << 4);\n");
                fprintf(out, "      _v = ((_v >> 8) & 0x00FF00FF) | ((_v & 0x00FF00FF) << 8);\n");
                fprintf(out, "      %s = (_v >> 16) | (_v << 16); }\n", r2);
            }
            break;
        case 0x0B: /* TRNC.SW */
            if (insn->reg2 != 0) {
                fprintf(out, "    { float _f; memcpy(&_f, &%s, 4); %s = (uint32_t)(int32_t)truncf(_f); }\n", r1, r2);
            }
            break;
        case 0x0C: /* MPYHW */
            if (insn->reg2 != 0) {
                fprintf(out, "    %s = (uint32_t)((int32_t)(int16_t)%s * (int32_t)(int16_t)%s);\n",
                        r2, r1, r2);
            }
            break;
        default:
            fprintf(out, "    /* UNKNOWN subop 0x%02X */\n", insn->subop);
            break;
        }
        break;

    default:
        fprintf(out, "    /* UNKNOWN opcode 0x%02X */\n", insn->opcode);
        break;
    }
}

static void label_set_add(label_set_t *ls, uint32_t addr) {
    /* Cap at 4096 labels per function */
    if (ls->count >= 4096) return;
    for (int i = 0; i < ls->count; i++) {
        if (ls->addrs[i] == addr) return;
    }
    if (ls->count >= ls->cap) {
        ls->cap = ls->cap * 2;
        if (ls->cap > 4096) ls->cap = 4096;
        uint32_t *new_addrs = realloc(ls->addrs, ls->cap * sizeof(uint32_t));
        if (!new_addrs) return;
        ls->addrs = new_addrs;
    }
    ls->addrs[ls->count++] = addr;
}

static bool label_set_has(label_set_t *ls, uint32_t addr) {
    for (int i = 0; i < ls->count; i++) {
        if (ls->addrs[i] == addr) return true;
    }
    return false;
}

/* Convert address to ROM offset, handling high addresses */
static uint32_t resolve_addr(v810_ctx_t *ctx, uint32_t addr) {
    if (addr >= 0x08000000) {
        addr = addr & 0x07FFFFFF;
    }
    return addr - ctx->rom_base;
}

/* Emit one function */
static void emit_function(v810_ctx_t *ctx, FILE *out, int func_idx) {
    v810_func_t *func = &ctx->funcs[func_idx];

    /* Bounds check */
    if (func->addr < ctx->rom_base || func->addr >= ROM_REGION_END) return;
    if (func->end_addr < ctx->rom_base || func->end_addr > ROM_REGION_END) return;

    uint32_t start_off = resolve_addr(ctx, func->addr);
    uint32_t end_off = resolve_addr(ctx, func->end_addr);

    if (start_off >= ctx->rom_size || end_off > ctx->rom_size) return;
    if (start_off >= end_off) return;

    /* Only emit functions confirmed reachable from known entry points */
    if (!func->confirmed) return;

    /* Cap function size to avoid pathological cases from false JAL targets */
    uint32_t func_size = end_off - start_off;
    if (func_size > 0x10000) { /* 64KB max */
        fprintf(stderr, "  warning: func_%08X is %u bytes, capping at 64KB\n",
                func->addr, func_size);
        end_off = start_off + 0x10000;
    }

    /* Catalogue stack-pointer loads before walking the body, and mark each as
     * a switch or a restore by looking for a matching store (see g_sp_loads). */
    g_num_sp_loads = 0;
    for (uint32_t o = start_off; o < end_off && o < ctx->rom_size; ) {
        v810_insn_t si;
        if (!v810_decode(ctx->rom, o, ctx->rom_size, &si)) break;
        if (si.opcode == 0x33 && si.reg2 == 3 && si.reg1 != 3
            && g_num_sp_loads < MAX_SP_LOADS) {
            bool matched = false;
            for (uint32_t p = start_off; p < end_off && p < ctx->rom_size; ) {
                v810_insn_t pi;
                if (!v810_decode(ctx->rom, p, ctx->rom_size, &pi)) break;
                if (pi.opcode == 0x37 && pi.reg2 == 3 && pi.reg1 == si.reg1
                    && pi.imm == si.imm) { matched = true; break; }
                p += pi.size;
            }
            g_sp_loads[g_num_sp_loads].addr = ROM_OFF_TO_ADDR(o, ctx->rom_base);
            g_sp_loads[g_num_sp_loads].is_switch = !matched;
            g_num_sp_loads++;
        }
        o += si.size;
    }

    /* First pass: collect all branch targets for labels */
    label_set_t labels = { .addrs = malloc(256 * sizeof(uint32_t)), .count = 0, .cap = 256 };

    uint32_t off = start_off;
    while (off < end_off && off < ctx->rom_size) {
        v810_insn_t insn;
        if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
        insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);

        /* Branches within this function need labels */
        if (insn.format == FMT_III && insn.cond != 0x0D) {
            uint32_t btarget = insn.addr + insn.imm;
            if (btarget >= func->addr && btarget < func->end_addr) {
                label_set_add(&labels, btarget);
            }
            /* External Bcond targets handled in emit via find_func_by_addr */
        } else if (insn.opcode == 0x2A) { /* JR */
            uint32_t target = insn.addr + insn.imm;
            if (target >= func->addr && target < func->end_addr) {
                label_set_add(&labels, target);
            }
            /* External targets handled in emit via find_func_by_addr */
        } else if (insn.opcode == 0x06 && insn.reg1 != 31) { /* JMP [reg] */
            uint32_t resolved = lookup_resolved_jump(ctx, insn.addr);
            if (resolved && find_func_by_addr(ctx, resolved) < 0
                         && resolved >= func->addr && resolved < func->end_addr) {
                label_set_add(&labels, resolved);
            }
        } else if (insn.opcode == 0x2B) { /* JAL - skip inline data */
            uint32_t target = insn.addr + insn.imm;
            off += v810_get_skip_bytes(ctx, target);
        }

        off += insn.size;
    }

    /* Emit function definition with a short header comment. */
    fprintf(out, "\n/* sub_%08X: 0x%08X-0x%08X (%u bytes)",
            func->addr, func->addr, func->end_addr, func->end_addr - func->addr);
    if (func->is_interrupt) {
        fprintf(out, ", IRQ level %d handler", func->int_level);
    }
    fprintf(out, " */\n");
    /* Renamed functions are emitted under the _real suffix so a hand-written
     * driver can define vb_func_<addr> as a wrapper/HLE replacement. */
    if (v810_is_renamed(ctx, func->addr))
        fprintf(out, "void vb_func_%08X_real(void) {\n", func->addr);
    else
        fprintf(out, "void vb_func_%08X(void) {\n", func->addr);

    /* Interrupt check at function entry, and a trace record so a boot chain
     * that simply ends can say what ran last -- the backtrace the generated
     * code otherwise does not have. */
    /* Consume the parked entry FIRST. vb_interrupt_check() below can run an
     * entire interrupt handler, and every function it enters would otherwise
     * see this transfer's target, fail to match it, and discard it. */
    fprintf(out, "    uint32_t _e = vb_recomp_entry; vb_recomp_entry = 0;\n");
    fprintf(out, "    vb_interrupt_check();\n");
    fprintf(out, "    vb_recomp_trace(0x%08Xu);\n", func->addr);
    /* A transfer to an address INSIDE this function enters here with the
     * target parked in vb_recomp_entry; the dispatch block at the bottom
     * turns it into a goto. Without this the target is not in the function
     * table and the transfer is dropped -- Mario Clash drops 129,000 of them
     * in a 600-frame run, all four hot addresses interior to a known
     * function. Entering mid-function deliberately skips the prologue,
     * which is what the hardware does too. */
    fprintf(out, "    if (_e) goto vb_entry_dispatch;\n");

    /* Second pass: emit code, tracking which labels get declared.
     * Branch targets that fall mid-instruction (decoder skipped over them
     * because we walked instruction-by-instruction) need an orphan stub
     * emitted at end of function so the goto compiles. */
    label_set_t declared = { .addrs = malloc(256 * sizeof(uint32_t)), .count = 0, .cap = 256 };
    off = start_off;
    int insn_count = 0;
    bool last_terminates = false;
    while (off < end_off && off < ctx->rom_size) {
        v810_insn_t insn;
        if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
        insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);

        /* Emit label if this address is a branch target */
        if (label_set_has(&labels, insn.addr)) {
            fprintf(out, "label_%08X:\n", insn.addr);
            fprintf(out, "    vb_recomp_pc = 0x%08Xu;\n", insn.addr);
            label_set_add(&declared, insn.addr);
            /* Add interrupt check at backward branch targets */
            if (insn.addr < func->addr + (end_off - start_off)) {
                fprintf(out, "    vb_interrupt_check();\n");
            }
        }

        emit_insn(ctx, out, &insn, &labels);
        /* Does control leave the function here, or fall through to the next
         * instruction? Only an unconditional transfer ends the flow. */
        last_terminates = (insn.opcode == 0x06)                       /* jmp [reg] */
                       || (insn.opcode == 0x2A)                       /* jr        */
                       || (insn.opcode == 0x19)                       /* reti      */
                       || (insn.format == FMT_III && insn.cond == 0x05); /* br     */
        off += insn.size;
        if (insn.opcode == 0x2B) { /* JAL - skip inline data, don't emit it as code */
            uint32_t target = insn.addr + insn.imm;
            off += v810_get_skip_bytes(ctx, target);
        }
        insn_count++;

        /* Safety: cap at 10000 instructions per function */
        if (insn_count > 10000) {
            fprintf(out, "    /* TRUNCATED: too many instructions */\n");
            break;
        }
    }

    /* A body that does not end in a transfer FALLS THROUGH on hardware, into
     * whatever follows it in ROM. Emitting nothing here silently turns that
     * fall-through into a return -- which is exactly what a function SPLIT
     * creates, since cutting a function in two leaves the first half ending
     * mid-flow. Hand control to the next address instead -- but ONLY when that
     * address is already a function. Emitting it unconditionally makes every
     * function's end a jump target, which gets registered as a function, which
     * has its own fall-through: SD Gundam ran past 83 MB of generated C that
     * way. */
    if (!last_terminates && find_func_by_addr(ctx, func->end_addr) >= 0) {
        fprintf(out, "    VB_JUMP(0x%08Xu); /* falls through */\n", func->end_addr);
    }

    /* Emit orphan label stubs for any branch target that wasn't an
     * instruction boundary in our walk (would otherwise be undefined). */
    for (int i = 0; i < labels.count; i++) {
        if (!label_set_has(&declared, labels.addrs[i])) {
            fprintf(out, "label_%08X:\n", labels.addrs[i]);
            fprintf(out, "    return; /* orphan label - target not on instruction boundary */\n");
        }
    }

        /* Entry dispatch. Only labels the body actually declared are reachable;
     * anything else stays a counted stub rather than restarting the function
     * from the top, which would re-run the prologue and unbalance the stack. */
    fprintf(out, "vb_entry_dispatch:\n");
    fprintf(out, "    {\n");
    fprintf(out, "      switch (_e) {\n");
    for (int i = 0; i < declared.count; i++)
        fprintf(out, "      case 0x%08Xu: goto label_%08X;\n",
                declared.addrs[i], declared.addrs[i]);
    fprintf(out, "      default: vb_recomp_stub(_e); return;\n");
    fprintf(out, "      } }\n");
fprintf(out, "}\n");
    free(labels.addrs);
    free(declared.addrs);
}

void v810_emit_c(v810_ctx_t *ctx, FILE *out) {
    /* File header */
    fprintf(out, "/*\n");
    fprintf(out, " * Auto-generated by v810recomp - DO NOT EDIT\n");
    fprintf(out, " *\n");
    fprintf(out, " * Each vb_func_<addr> is one recompiled V810 routine. Every line is\n");
    fprintf(out, " * preceded by its original address and disassembly. CPU state lives in\n");
    fprintf(out, " * the global `vb_cpu`:\n");
    fprintf(out, " *   vb_cpu.r[0]  = zero (hardwired)   vb_cpu.r[3]  = sp (stack pointer)\n");
    fprintf(out, " *   vb_cpu.r[4]  = gp (global ptr)    vb_cpu.r[5]  = tp (text ptr)\n");
    fprintf(out, " *   vb_cpu.r[31] = lp (link/return)   vb_cpu.sr[5] = psw (flags)\n");
    fprintf(out, " * Flag-setting ops call helpers in <vbrecomp/cpu.h> (vb_add, vb_sub,\n");
    fprintf(out, " * vb_cmp, vb_shl, ...) which update the PSW exactly as hardware does.\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#include <vbrecomp/vbrecomp.h>\n");
    fprintf(out, "#include <string.h>\n");
    fprintf(out, "#include <math.h>\n\n");

    /* Runtime dispatch for indirect jumps the static analysis can't resolve;
     * defined at the bottom of this file. */
    fprintf(out, "void vb_recomp_call(uint32_t addr);\n\n");
    fprintf(out, "void vb_recomp_call(uint32_t addr);\n");
    fprintf(out, "static void vb_recomp_call_inner(uint32_t addr);\n");
    fprintf(out, "static int vb_call_depth;\n");
    fprintf(out, "/* A transfer INTO A FRAME THAT IS STILL ACTIVE must unwind to it, not run as\n");
    fprintf(out, " * a nested call. Galactic Pinball's loop body tail-jumps back to its caller's\n");
    fprintf(out, " * `add -1, r25` at 0x07F000B4; dispatched as a nested call, that runs the\n");
    fprintf(out, " * loop AND lets the original call site return into the caller's own copy of\n");
    fprintf(out, " * it, so the loop advances about four times per iteration and r25 walks\n");
    fprintf(out, " * past zero (observed 0xFFFFFF9F). Park the target, unwind to depth 0, then\n");
    fprintf(out, " * dispatch it. */\n");
    fprintf(out, "static uint32_t vb_frames[256];\n");
    fprintf(out, "static int vb_unwinding;\n");
    fprintf(out, "static uint32_t vb_interior_owner(uint32_t a);\n");
    fprintf(out, "static int vb_frame_active(uint32_t f) {\n");
    fprintf(out, "    for (int i = 0; i < vb_call_depth && i < 256; i++)\n");
    fprintf(out, "        if (vb_frames[i] == f) return 1;\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "/* Control transfer out of a function.\n");
    fprintf(out, " *\n");
    fprintf(out, " * A game main loop is a CYCLE: a state handler ends by jumping back to\n");
    fprintf(out, " * its dispatcher. Emitting that jump as a call is unbounded mutual\n");
    fprintf(out, " * recursion; emitting it as a return drops the loop and the boot unwinds\n");
    fprintf(out, " * after one state. So a jump parks its target and returns, and every CALL\n");
    fprintf(out, " * SITE drains the chain -- which runs it to completion BEFORE the caller\n");
    fprintf(out, " * resumes, keeping ordering right and the stack flat. */\n");
    fprintf(out, "void vb_recomp_drain(void) {\n");
    fprintf(out, "    while (vb_recomp_tail_target) {\n");
    fprintf(out, "        /* Too deep to keep going here: leave the target PARKED and\n");
    fprintf(out, "         * return. Every VB_CALL drains, so an outer, shallower drain\n");
    fprintf(out, "         * picks it up and the cycle continues iteratively instead of\n");
    fprintf(out, "         * growing the C stack. Galactic Pinball and 3-D Tetris both\n");
    fprintf(out, "         * died with STATUS_STACK_OVERFLOW re-entering their main\n");
    fprintf(out, "         * function through a drain nested inside it. Ordering is\n");
    fprintf(out, "         * unaffected below the limit, which is why this works where\n");
    fprintf(out, "         * draining only at depth 0 did not. */\n");
    fprintf(out, "        if (vb_call_depth > 96) return;\n");
    fprintf(out, "        uint32_t t = vb_recomp_tail_target;\n");
    fprintf(out, "        /* A drained target was JUMPED to, not called: dispatch it\n");
    fprintf(out, "         * WITHOUT raising the depth, or its own `jmp [r31]` reads as a\n");
    fprintf(out, "         * return and the transfer chain unwinds instead of continuing\n");
    fprintf(out, "         * (StarFox boots six frames and stops). The loop is iterative,\n");
    fprintf(out, "         * so a flat depth costs no stack. */\n");
    fprintf(out, "        uint32_t _own = vb_interior_owner(t);\n");
    fprintf(out, "        if (_own && vb_call_depth > 0 && vb_frame_active(_own)) {\n");
    fprintf(out, "            vb_unwinding = 1;   /* leave it parked and unwind first */\n");
    fprintf(out, "            return;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        vb_recomp_tail_target = 0;\n");
    fprintf(out, "        vb_recomp_call_inner(t);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "/* `jmp [r31]` is a RETURN only when a jal frame is open. A function\n");
    fprintf(out, " * entered by a TRANSFER has no caller, so its r31 holds a continuation\n");
    fprintf(out, " * and the instruction is a jump -- emitting a plain return there drops\n");
    fprintf(out, " * control, which is where StarFox's boot chain ends (sub_07FE0300).\n");
    fprintf(out, " * Only the run time knows which case applies, so count jal depth. */\n");
    fprintf(out, "#define VB_CALL(f,a) do { vb_frames[vb_call_depth < 256 ? vb_call_depth : 255] = (a); if (++vb_call_depth > 128 && vb_call_depth < 160) vb_recomp_deep((a), vb_call_depth); (f)(); --vb_call_depth; if (vb_unwinding) { if (vb_call_depth > 0) return; vb_unwinding = 0; } vb_recomp_drain(); } while (0)\n");
    fprintf(out, "#define VB_RET()    do { if (vb_call_depth == 0) \\\n");
    fprintf(out, "                             vb_recomp_tail_target = vb_cpu.r[31] & ~1u; \\\n");
    fprintf(out, "                         return; } while (0)\n");
    fprintf(out, "/* A jump to address 0 is never legitimate, and parking it would be\n");
    fprintf(out, " * invisible: the drain loop treats a zero target as `nothing pending` and\n");
    fprintf(out, " * exits, so control is lost with no counter moving. Count it as an\n");
    fprintf(out, " * unimplemented call instead -- it then shows up in the heartbeat and\n");
    fprintf(out, " * scripts/discover.ps1 can see it. */\n");
    fprintf(out, "#define VB_JUMP(a)  do { uint32_t _t_ = (a); if (_t_) vb_recomp_tail_target = _t_; else vb_recomp_stub(0); return; } while (0)\n");
    fprintf(out, "\n");

    /* Forward declarations (confirmed functions only) */
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (!ctx->funcs[i].confirmed) continue;
        fprintf(out, "void vb_func_%08X(void);\n", ctx->funcs[i].addr);
    }
    /* Also declare jump table targets */
    for (int jt = 0; jt < ctx->num_jump_tables; jt++) {
        for (int e = 0; e < ctx->jump_tables[jt].num_entries; e++) {
            uint32_t t = ctx->jump_tables[jt].targets[e];
            bool already = false;
            for (int i = 0; i < ctx->num_funcs; i++) {
                if (ctx->funcs[i].addr == t && ctx->funcs[i].confirmed) {
                    already = true; break;
                }
            }
            /* Check for duplicates within jump tables too */
            if (!already) {
                for (int jt2 = 0; jt2 < jt; jt2++) {
                    for (int e2 = 0; e2 < ctx->jump_tables[jt2].num_entries; e2++) {
                        if (ctx->jump_tables[jt2].targets[e2] == t) { already = true; break; }
                    }
                    if (already) break;
                }
                for (int e2 = 0; e2 < e; e2++) {
                    if (ctx->jump_tables[jt].targets[e2] == t) { already = true; break; }
                }
            }
            if (!already) {
                fprintf(out, "void vb_func_%08X(void);\n", t);
            }
        }
    }
    /* Declare ALL JAL targets from confirmed function bodies */
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (!ctx->funcs[i].confirmed || !ctx->funcs[i].visited) continue;
        if (ctx->funcs[i].addr < ctx->rom_base) continue;
        uint32_t foff = ctx->funcs[i].addr;
        foff = v810_normalize_rom_addr(ctx, foff);
        foff -= ctx->rom_base;
        uint32_t fend = foff + (ctx->funcs[i].end_addr - ctx->funcs[i].addr);
        if (foff >= ctx->rom_size || fend > ctx->rom_size) continue;
        uint32_t off = foff;
        while (off < fend && off < ctx->rom_size) {
            v810_insn_t insn;
            if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
            insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);
            if (insn.opcode == 0x2B) {
                uint32_t target = insn.addr + insn.imm;
                target = v810_normalize_rom_addr(ctx, target);
                if (target >= ctx->rom_base && target < ROM_REGION_END) {
                    fprintf(out, "void vb_func_%08X(void);\n", target);
                }
            }
            off += insn.size;
        }
    }
    fprintf(out, "\n");

    /* Emit each function */
    int emitted = 0;
    bool *was_emitted = calloc(ctx->num_funcs, sizeof(bool));
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (!ctx->funcs[i].confirmed) continue;

        /* Check if function has code to emit */
        uint32_t start_off = 0, end_off = 0;
        bool valid = false;
        if (ctx->funcs[i].addr >= ctx->rom_base && ctx->funcs[i].addr < ROM_REGION_END &&
            ctx->funcs[i].end_addr > ctx->funcs[i].addr) {
            start_off = ctx->funcs[i].addr - ctx->rom_base;
            if (ctx->funcs[i].addr >= 0x08000000)
                start_off = (ctx->funcs[i].addr & 0x07FFFFFF) - ctx->rom_base;
            end_off = start_off + (ctx->funcs[i].end_addr - ctx->funcs[i].addr);
            if (start_off < ctx->rom_size && end_off <= ctx->rom_size && start_off < end_off)
                valid = true;
        }

        emit_function(ctx, out, i);

        /* Check if emit_function actually wrote something */
        if (valid && ctx->funcs[i].visited) {
            was_emitted[i] = true;
            emitted++;
        }
    }

    /* Generate stubs for any referenced-but-undefined functions.
     * This includes jump table targets that were confirmed but
     * not analyzed/emitted, and functions referenced by switch cases. */
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (was_emitted[i]) continue;
        /* Check if this function was forward-declared (confirmed) */
        if (!ctx->funcs[i].confirmed) continue;
        /* Keep a renamed function's stub under _real too, so the driver's
         * vb_func_<addr> wrapper stays the only plain-named definition. */
        if (v810_is_renamed(ctx, ctx->funcs[i].addr))
            fprintf(out, "\nvoid vb_func_%08X_real(void) {\n", ctx->funcs[i].addr);
        else
            fprintf(out, "\nvoid vb_func_%08X(void) {\n", ctx->funcs[i].addr);
        fprintf(out, "    /* STUB: function not yet analyzed */\n");
        fprintf(out, "    vb_recomp_stub(0x%08Xu);\n", ctx->funcs[i].addr);
        fprintf(out, "}\n");
    }

    /* Generate stubs for ALL referenced but undefined functions.
     * Scan confirmed function bodies for JAL targets and jump table entries. */
    {
        uint32_t *emitted_addrs = malloc((ctx->num_funcs + 2048) * sizeof(uint32_t));
        int n_emitted = 0;

        /* Collect everything we've already emitted */
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (ctx->funcs[i].confirmed) {
                emitted_addrs[n_emitted++] = ctx->funcs[i].addr;
            }
        }

        /* Scan jump table targets */
        for (int jt = 0; jt < ctx->num_jump_tables; jt++) {
            for (int e = 0; e < ctx->jump_tables[jt].num_entries; e++) {
                uint32_t t = ctx->jump_tables[jt].targets[e];
                bool already = false;
                for (int k = 0; k < n_emitted; k++) {
                    if (emitted_addrs[k] == t) { already = true; break; }
                }
                if (!already) {
                    fprintf(out, "\nvoid vb_func_%08X(void) { /* STUB: jump table target */ vb_recomp_stub(0x%08Xu); }\n", t, t);
                    emitted_addrs[n_emitted++] = t;
                }
            }
        }

        /* Scan all confirmed function bodies for JAL targets that need stubs */
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (!ctx->funcs[i].confirmed || !ctx->funcs[i].visited) continue;
            if (ctx->funcs[i].addr < ctx->rom_base) continue;
            uint32_t foff = ctx->funcs[i].addr;
            foff = v810_normalize_rom_addr(ctx, foff);
            foff -= ctx->rom_base;
            uint32_t fend = foff + (ctx->funcs[i].end_addr - ctx->funcs[i].addr);
            if (foff >= ctx->rom_size || fend > ctx->rom_size) continue;

            uint32_t off = foff;
            while (off < fend && off < ctx->rom_size) {
                v810_insn_t insn;
                if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
                insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);

                if (insn.opcode == 0x2B) { /* JAL */
                    uint32_t target = insn.addr + insn.imm;
                    target = v810_normalize_rom_addr(ctx, target);
                    bool already = false;
                    for (int k = 0; k < n_emitted; k++) {
                        if (emitted_addrs[k] == target) { already = true; break; }
                    }
                    if (!already && target >= ctx->rom_base && target < ROM_REGION_END) {
                        fprintf(out, "\nvoid vb_func_%08X(void) { /* STUB: callee */ vb_recomp_entry = 0; vb_recomp_stub(0x%08Xu); }\n", target, target);
                        emitted_addrs[n_emitted++] = target;
                    }
                }
                off += insn.size;
            }
        }
        /* --- Runtime dispatch table -------------------------------------
         * Every defined function, sorted by address, so unresolved indirect
         * jumps/calls can be dispatched by address at run time. */
        qsort(emitted_addrs, n_emitted, sizeof(uint32_t), cmp_u32);

        fprintf(out, "\n/* Runtime dispatch table: ROM address -> recompiled function. */\n");
        fprintf(out, "typedef void (*vb_func_ptr_t)(void);\n");
        fprintf(out, "static const struct { uint32_t addr; vb_func_ptr_t fn; } vb_func_table[] = {\n");
        int table_count = 0;
        for (int i = 0; i < n_emitted; i++) {
            if (i > 0 && emitted_addrs[i] == emitted_addrs[i - 1]) continue; /* dedup */
            fprintf(out, "    { 0x%08Xu, vb_func_%08X },\n", emitted_addrs[i], emitted_addrs[i]);
            table_count++;
        }
        fprintf(out, "};\n");
        fprintf(out, "static const int vb_func_table_count = %d;\n\n", table_count);

        /* Interior entries: a confirmed function whose first byte was traced by
         * a DIFFERENT function. The drain needs this to tell a transfer into an
         * active frame from an ordinary call. */
        fprintf(out, "static uint32_t vb_interior_owner(uint32_t a) {\n");
        fprintf(out, "    switch (a) {\n");
        {
            int n_int = 0;
            for (int i = 0; i < ctx->num_funcs; i++) {
                if (!ctx->funcs[i].confirmed) continue;
                uint32_t fo = resolve_addr(ctx, ctx->funcs[i].addr);
                if (fo >= ctx->rom_size) continue;
                uint32_t own = ctx->byte_owner[fo];
                if (!own || own == ctx->funcs[i].addr) continue;
                bool own_ok = false;
                for (int k = 0; k < ctx->num_funcs && !own_ok; k++)
                    if (ctx->funcs[k].addr == own && ctx->funcs[k].confirmed) own_ok = true;
                if (!own_ok) continue;
                fprintf(out, "    case 0x%08Xu: return 0x%08Xu;\n",
                        ctx->funcs[i].addr, own);
                n_int++;
            }
            fprintf(stderr, "  interior entries emitted: %d\n", n_int);
        }
        fprintf(out, "    default: return 0;\n    }\n}\n\n");

    fprintf(out, "/* Depth trace: a cycle that nests instead of running flat blows the\n");
    fprintf(out, " * stack, and the address printed here is the one doing it. */\n");
    fprintf(out, "static int vb_call_depth;\n");
    fprintf(out, "void vb_recomp_call(uint32_t addr) {\n");
    fprintf(out, "    if (++vb_call_depth > 128 && vb_call_depth < 160)\n");
    fprintf(out, "        vb_recomp_deep(addr, vb_call_depth);\n");
    fprintf(out, "    vb_recomp_call_inner(addr);\n");
    fprintf(out, "    --vb_call_depth;\n");
    fprintf(out, "}\n");
    fprintf(out, "static void vb_recomp_call_inner(uint32_t addr) {\n");
        fprintf(out, "    if (addr >= 0x08000000u) addr &= 0x07FFFFFFu;\n");
        if (ctx->rom_base > ROM_REGION_BASE) {
            /* Collapse ROM-internal mirrors below rom_base to canonical range. */
            fprintf(out, "    if (addr >= 0x%08Xu && addr < 0x%08Xu)\n",
                    ROM_REGION_BASE, ctx->rom_base);
            fprintf(out, "        addr = 0x%08Xu + (addr - 0x%08Xu) %% 0x%Xu;\n",
                    ctx->rom_base, ROM_REGION_BASE, ctx->rom_size);
        }
        fprintf(out, "    int lo = 0, hi = vb_func_table_count - 1;\n");
        fprintf(out, "    while (lo <= hi) {\n");
        fprintf(out, "        int mid = (lo + hi) >> 1;\n");
        fprintf(out, "        uint32_t a = vb_func_table[mid].addr;\n");
        fprintf(out, "        if (a == addr) { vb_func_table[mid].fn(); return; }\n");
        fprintf(out, "        if (a < addr) lo = mid + 1; else hi = mid - 1;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    /* No exact match. The greatest function below the target\n");
        fprintf(out, "     * encloses it if anything does: park the address and let\n");
        fprintf(out, "     * that function's entry dispatch decide. Its switch over\n");
        fprintf(out, "     * the labels it actually declared IS the range check, so a\n");
        fprintf(out, "     * bogus address still hits the default and is counted --\n");
        fprintf(out, "     * which is all this used to do, for 129,000 transfers a run\n");
        fprintf(out, "     * in Mario Clash alone. Not inside an interrupt handler,\n");
        fprintf(out, "     * though: diverting one mid-flight abandons its C frame, so\n");
        fprintf(out, "     * it never reaches RETI and EP stays set forever -- Mario\n");
        fprintf(out, "     * Clash wedges that way, leaking exactly the vector stub's\n");
        fprintf(out, "     * one push. */\n");
        fprintf(out, "    if (lo > 0 && vb_func_table[lo-1].addr != addr\n");
        fprintf(out, "        && !(vb_cpu.sr[VB_SREG_PSW] & VB_PSW_EP)) {\n");
        fprintf(out, "        vb_recomp_entry = addr;\n");
        fprintf(out, "        vb_func_table[lo-1].fn();\n");
        fprintf(out, "        return;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    vb_recomp_stub(addr);\n");
        fprintf(out, "}\n");

        /* --- Generic boot helpers ---------------------------------------
         * Let a generic driver boot any game: register the interrupt
         * handlers the analyzer discovered, and run the reset vector. */
        fprintf(out, "\n/* Register the interrupt handlers discovered by v810recomp. */\n");
        fprintf(out, "void vb_recomp_init_handlers(void) {\n");
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (!ctx->funcs[i].is_interrupt) continue;
            static const char *lvl_name[] = { "KEY", "TIMER", "EXPANSION", "LINK", "VIP" };
            int lv = ctx->funcs[i].int_level;
            const char *nm = (lv >= 0 && lv <= 4) ? lvl_name[lv] : "?";
            fprintf(out, "    vb_interrupt_set_handler(%d, vb_func_%08X); /* %s */\n",
                    lv, ctx->funcs[i].addr, nm);
        }
        fprintf(out, "}\n");
        fprintf(out, "\n/* Run the reset vector (boots the game). */\n");
        fprintf(out, "void vb_recomp_boot(void) { VB_CALL(vb_func_07FFFFF0, 0x07FFFFF0u); }\n");

        free(emitted_addrs);
    }
    free(was_emitted);
    fprintf(stderr, "\n");
}

void v810_emit_header(v810_ctx_t *ctx, FILE *out) {
    fprintf(out, "/*\n");
    fprintf(out, " * Auto-generated by v810recomp - DO NOT EDIT\n");
    fprintf(out, " */\n\n");
    fprintf(out, "#ifndef VB_RECOMP_FUNCS_H\n");
    fprintf(out, "#define VB_RECOMP_FUNCS_H\n\n");

    for (int i = 0; i < ctx->num_funcs; i++) {
        if (!ctx->funcs[i].confirmed) continue;
        fprintf(out, "void vb_func_%08X(void);\n", ctx->funcs[i].addr);
    }

    fprintf(out, "\n/* Generic boot helpers (see games/driver/main.c). */\n");
    fprintf(out, "void vb_recomp_call(uint32_t addr);   /* dispatch by ROM address */\n");
    fprintf(out, "void vb_recomp_init_handlers(void);   /* register discovered IRQ handlers */\n");
    fprintf(out, "void vb_recomp_boot(void);            /* run the reset vector */\n");

    fprintf(out, "\n#endif\n");
}
