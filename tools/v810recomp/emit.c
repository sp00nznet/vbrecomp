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
    fprintf(out, "    %s = vb_add(%s, %s);\n", result, a, b);
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
static int find_func_by_addr(v810_ctx_t *ctx, uint32_t addr) {
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (ctx->funcs[i].addr == addr) return i;
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
        }
        break;

    case 0x02: /* SUB reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_sub(out, r2, r1, r2);
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
        if (insn->reg1 == 31) {
            fprintf(out, "    return; /* jmp [r31] */\n");
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
                        fprintf(out, "    case 0x%08Xu: vb_func_%08X(); break;\n", raw, mapped);
                    }
                }
                fprintf(out, "    default: break; /* unhandled */\n");
                fprintf(out, "    }\n");
                /* After dispatch, the stub JR's back to the calling loop.
                 * Since the stub returns to us, continue to next instruction
                 * (fall through, don't return). */
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
                    fprintf(out, "    vb_recomp_call(0x%08Xu); return; /* resolved jmp -> dispatch */\n",
                            resolved);
                }
            } else {
                /* Unresolved at compile time: dispatch through the runtime
                 * function table by the register's value (tail call). */
                fprintf(out, "    vb_cpu.pc = %s & 0xFFFFFFFE;\n", r1);
                fprintf(out, "    vb_recomp_call(vb_cpu.pc); return; /* indirect jump [%s] */\n", r1);
            }
        }
        break;

    case 0x07: /* SAR reg1, reg2 */
        if (insn->reg2 != 0) {
            emit_flags_shift(out, r2, r1, ">>", true);
        }
        break;

    case 0x08: /* MUL reg1, reg2 */
        fprintf(out, "    %s = vb_mul(%s, %s);\n", r2, r2, r1);
        break;

    case 0x09: /* DIV reg1, reg2 */
        fprintf(out, "    if (%s != 0) %s = vb_div(%s, %s);\n", r1, r2, r2, r1);
        break;

    case 0x0A: /* MULU reg1, reg2 */
        fprintf(out, "    %s = vb_mulu(%s, %s);\n", r2, r2, r1);
        break;

    case 0x0B: /* DIVU reg1, reg2 */
        fprintf(out, "    if (%s != 0) %s = vb_divu(%s, %s);\n", r1, r2, r2, r1);
        break;

    case 0x0C: /* OR */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s | %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
        }
        break;

    case 0x0D: /* AND */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s & %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
        }
        break;

    case 0x0E: /* XOR */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s ^ %s;\n", r2, r2, r1);
            emit_flags_zs(out, r2);
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

    case 0x1F: /* Bitstring ops */
        fprintf(out, "    /* TODO: bitstring operation %d */\n", insn->reg1);
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
                    fprintf(out, "    vb_func_%08X(); return;\n", target);
                } else {
                    fprintf(out, "    if (%s) { vb_func_%08X(); return; }\n",
                            cond_expr[insn->cond], target);
                }
            } else {
                /* External branch to unknown code */
                if (insn->cond == 0x05) {
                    fprintf(out, "    return; /* branch to 0x%08X outside function */\n", target);
                } else {
                    fprintf(out, "    if (%s) return; /* branch to 0x%08X */\n",
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
        } else if (find_func_by_addr(ctx, target) >= 0) {
            fprintf(out, "    vb_func_%08X(); return; /* tail call */\n", target);
        } else {
            /* External JR to unknown code — likely shared prologue or
             * code above function start. Emit as unresolved return. */
            fprintf(out, "    /* WARNING: JR to 0x%08X outside function */\n", target);
            fprintf(out, "    return;\n");
        }
        break;
    }

    case 0x2B: { /* JAL */
        uint32_t target = insn->addr + insn->imm;
        uint32_t skip = v810_get_skip_bytes(ctx, target);
        fprintf(out, "    vb_cpu.r[31] = 0x%08X;\n", insn->addr + 4 + skip);
        /* Normalize the target the same way declarations/stubs do. An
         * in-ROM target is a direct call; an out-of-ROM one (a mirror or a
         * garbage target from data decoded as JAL) has no emitted function,
         * so dispatch it at run time (no-op on miss) to stay linkable. */
        uint32_t nt = target;
        if (nt >= 0x08000000) nt &= 0x07FFFFFF;
        if (nt >= ctx->rom_base && nt < ROM_REGION_END) {
            fprintf(out, "    vb_func_%08X();\n", nt);
        } else {
            fprintf(out, "    vb_recomp_call(0x%08Xu);\n", target);
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
        }
        break;

    case 0x2D: /* ANDI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s & 0x%04X;\n", r2, r1, (uint16_t)insn->imm);
            emit_flags_zs(out, r2);
        }
        break;

    case 0x2E: /* XORI */
        if (insn->reg2 != 0) {
            fprintf(out, "    %s = %s ^ 0x%04X;\n", r2, r1, (uint16_t)insn->imm);
            emit_flags_zs(out, r2);
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

    /* Interrupt check at function entry */
    fprintf(out, "    vb_interrupt_check();\n");

    /* Second pass: emit code, tracking which labels get declared.
     * Branch targets that fall mid-instruction (decoder skipped over them
     * because we walked instruction-by-instruction) need an orphan stub
     * emitted at end of function so the goto compiles. */
    label_set_t declared = { .addrs = malloc(256 * sizeof(uint32_t)), .count = 0, .cap = 256 };
    off = start_off;
    int insn_count = 0;
    while (off < end_off && off < ctx->rom_size) {
        v810_insn_t insn;
        if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
        insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);

        /* Emit label if this address is a branch target */
        if (label_set_has(&labels, insn.addr)) {
            fprintf(out, "label_%08X:\n", insn.addr);
            label_set_add(&declared, insn.addr);
            /* Add interrupt check at backward branch targets */
            if (insn.addr < func->addr + (end_off - start_off)) {
                fprintf(out, "    vb_interrupt_check();\n");
            }
        }

        emit_insn(ctx, out, &insn, &labels);
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

    /* Emit orphan label stubs for any branch target that wasn't an
     * instruction boundary in our walk (would otherwise be undefined). */
    for (int i = 0; i < labels.count; i++) {
        if (!label_set_has(&declared, labels.addrs[i])) {
            fprintf(out, "label_%08X:\n", labels.addrs[i]);
            fprintf(out, "    return; /* orphan label - target not on instruction boundary */\n");
        }
    }

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
        if (foff >= 0x08000000) foff &= 0x07FFFFFF;
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
                if (target >= 0x08000000) target &= 0x07FFFFFF;
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
                    fprintf(out, "\nvoid vb_func_%08X(void) { /* STUB: jump table target */ }\n", t);
                    emitted_addrs[n_emitted++] = t;
                }
            }
        }

        /* Scan all confirmed function bodies for JAL targets that need stubs */
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (!ctx->funcs[i].confirmed || !ctx->funcs[i].visited) continue;
            if (ctx->funcs[i].addr < ctx->rom_base) continue;
            uint32_t foff = ctx->funcs[i].addr;
            if (foff >= 0x08000000) foff &= 0x07FFFFFF;
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
                    if (target >= 0x08000000) target &= 0x07FFFFFF;
                    bool already = false;
                    for (int k = 0; k < n_emitted; k++) {
                        if (emitted_addrs[k] == target) { already = true; break; }
                    }
                    if (!already && target >= ctx->rom_base && target < ROM_REGION_END) {
                        fprintf(out, "\nvoid vb_func_%08X(void) { /* STUB: callee */ }\n", target);
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

        fprintf(out, "void vb_recomp_call(uint32_t addr) {\n");
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
        fprintf(out, "    /* No function at this address: unresolved target, no-op. */\n");
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
        fprintf(out, "void vb_recomp_boot(void) { vb_func_07FFFFF0(); }\n");

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
