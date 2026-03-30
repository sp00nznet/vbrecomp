/*
 * v810recomp - V810 instruction decoder
 * SPDX-License-Identifier: MIT
 */

#include "v810recomp.h"
#include <string.h>
#include <stdio.h>

/* Register names */
static const char *reg_names[32] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

/* Condition code names for Bcond */
static const char *cond_names[16] = {
    "bv",  "bl",  "bz",  "bnh", "bn",  "br",  "blt", "ble",
    "bnv", "bnl", "bnz", "bh",  "bp",  "nop", "bge", "bgt",
};

/* Read a 16-bit little-endian halfword from ROM */
static inline uint16_t read16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Sign extend from bit position */
static inline int32_t sign_ext(uint32_t val, int bits) {
    uint32_t mask = 1u << (bits - 1);
    return (int32_t)((val ^ mask) - mask);
}

bool v810_decode(const uint8_t *rom, uint32_t offset, uint32_t rom_size, v810_insn_t *out) {
    if (offset + 2 > rom_size) return false;

    memset(out, 0, sizeof(*out));
    uint16_t hw1 = read16(rom + offset);
    out->hw1 = hw1;
    out->opcode = (hw1 >> 10) & 0x3F;
    out->reg1 = hw1 & 0x1F;
    out->reg2 = (hw1 >> 5) & 0x1F;

    /* Determine format and size */
    if (out->opcode >= 0x28) {
        /* 32-bit instruction */
        if (offset + 4 > rom_size) return false;
        uint16_t hw2 = read16(rom + offset + 2);
        out->hw2 = hw2;
        out->size = 4;

        if (out->opcode == 0x3E) {
            /* Format VII: subopcode */
            out->format = FMT_VII;
            out->subop = (hw2 >> 10) & 0x3F;
        } else if (out->opcode == 0x2A || out->opcode == 0x2B) {
            /* Format IV: JR / JAL */
            out->format = FMT_IV;
            uint32_t raw = ((uint32_t)(hw1 & 0x3FF) << 16) | hw2;
            out->imm = sign_ext(raw, 26);
        } else if (out->opcode >= 0x30) {
            /* Format VI: load/store */
            out->format = FMT_VI;
            out->imm = (int16_t)hw2;
        } else {
            /* Format V: immediate operations */
            out->format = FMT_V;
            out->imm = (int16_t)hw2;  /* Will be adjusted per-opcode */
        }
    } else if ((out->opcode & 0x38) == 0x20) {
        /* Format III: Bcond (opcodes 0x20-0x27) */
        out->format = FMT_III;
        out->size = 2;
        out->cond = (hw1 >> 9) & 0xF;
        out->imm = sign_ext(hw1 & 0x1FF, 9);
    } else if (out->opcode >= 0x10) {
        /* Format II: immediate/special (0x10-0x1F) */
        out->format = FMT_II;
        out->size = 2;
        /* imm5 is in the reg1 field, sign-extended for most ops */
        out->imm = sign_ext(out->reg1, 5);
    } else {
        /* Format I: register-register (0x00-0x0F) */
        out->format = FMT_I;
        out->size = 2;
    }

    return true;
}

const char *v810_mnemonic(const v810_insn_t *insn) {
    switch (insn->opcode) {
    /* Format I */
    case 0x00: return "mov";
    case 0x01: return "add";
    case 0x02: return "sub";
    case 0x03: return "cmp";
    case 0x04: return "shl";
    case 0x05: return "shr";
    case 0x06: return "jmp";
    case 0x07: return "sar";
    case 0x08: return "mul";
    case 0x09: return "div";
    case 0x0A: return "mulu";
    case 0x0B: return "divu";
    case 0x0C: return "or";
    case 0x0D: return "and";
    case 0x0E: return "xor";
    case 0x0F: return "not";

    /* Format II */
    case 0x10: return "mov";
    case 0x11: return "add";
    case 0x12: return "setf";
    case 0x13: return "cmp";
    case 0x14: return "shl";
    case 0x15: return "shr";
    case 0x16: return "cli";
    case 0x17: return "sar";
    case 0x18: return "trap";
    case 0x19: return "reti";
    case 0x1A: return "halt";
    case 0x1C: return "ldsr";
    case 0x1D: return "stsr";
    case 0x1E: return "sei";
    case 0x1F: return "bitstring";

    /* Format III */
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
        return cond_names[insn->cond];

    /* Format IV */
    case 0x2A: return "jr";
    case 0x2B: return "jal";

    /* Format V */
    case 0x28: return "movea";
    case 0x29: return "addi";
    case 0x2C: return "ori";
    case 0x2D: return "andi";
    case 0x2E: return "xori";
    case 0x2F: return "movhi";

    /* Format VI */
    case 0x30: return "ld.b";
    case 0x31: return "ld.h";
    case 0x33: return "ld.w";
    case 0x34: return "st.b";
    case 0x35: return "st.h";
    case 0x37: return "st.w";
    case 0x38: return "in.b";
    case 0x39: return "in.h";
    case 0x3A: return "caxi";
    case 0x3B: return "in.w";
    case 0x3C: return "out.b";
    case 0x3D: return "out.h";
    case 0x3F: return "out.w";

    /* Format VII */
    case 0x3E:
        switch (insn->subop) {
        case 0x00: return "cmpf.s";
        case 0x02: return "cvt.ws";
        case 0x03: return "cvt.sw";
        case 0x04: return "addf.s";
        case 0x05: return "subf.s";
        case 0x06: return "mulf.s";
        case 0x07: return "divf.s";
        case 0x08: return "xb";
        case 0x09: return "xh";
        case 0x0A: return "rev";
        case 0x0B: return "trnc.sw";
        case 0x0C: return "mpyhw";
        default: return "???";
        }

    default: return "???";
    }
}

void v810_disasm(const v810_insn_t *insn, char *buf, int bufsize) {
    const char *mn = v810_mnemonic(insn);

    switch (insn->format) {
    case FMT_I:
        if (insn->opcode == 0x06) {
            /* JMP [reg1] */
            snprintf(buf, bufsize, "%-8s [%s]", mn, reg_names[insn->reg1]);
        } else if (insn->opcode == 0x0F) {
            /* NOT reg1, reg2 */
            snprintf(buf, bufsize, "%-8s %s, %s", mn, reg_names[insn->reg1], reg_names[insn->reg2]);
        } else {
            snprintf(buf, bufsize, "%-8s %s, %s", mn, reg_names[insn->reg1], reg_names[insn->reg2]);
        }
        break;

    case FMT_II:
        if (insn->opcode == 0x16 || insn->opcode == 0x1E) {
            /* CLI / SEI - no operands */
            snprintf(buf, bufsize, "%s", mn);
        } else if (insn->opcode == 0x19 || insn->opcode == 0x1A) {
            /* RETI / HALT */
            snprintf(buf, bufsize, "%s", mn);
        } else if (insn->opcode == 0x18) {
            /* TRAP vector */
            snprintf(buf, bufsize, "%-8s %d", mn, insn->reg1);
        } else if (insn->opcode == 0x1C) {
            /* LDSR reg2, regID */
            snprintf(buf, bufsize, "%-8s %s, sr%d", mn, reg_names[insn->reg2], insn->reg1);
        } else if (insn->opcode == 0x1D) {
            /* STSR regID, reg2 */
            snprintf(buf, bufsize, "%-8s sr%d, %s", mn, insn->reg1, reg_names[insn->reg2]);
        } else if (insn->opcode == 0x12) {
            /* SETF cond, reg2 */
            snprintf(buf, bufsize, "%-8s %s, %s", mn, cond_names[insn->reg1], reg_names[insn->reg2]);
        } else if (insn->opcode == 0x1F) {
            /* Bitstring */
            static const char *bs_names[] = {
                "sch0bsu","sch0bsd","sch1bsu","sch1bsd",
                "???","???","???","???",
                "orbsu","andbsu","xorbsu","movbsu",
                "ornbsu","andnbsu","xornbsu","notbsu",
            };
            int sub = insn->reg1;
            if (sub < 16) {
                snprintf(buf, bufsize, "%s", bs_names[sub]);
            } else {
                snprintf(buf, bufsize, "bitstring %d", sub);
            }
        } else {
            snprintf(buf, bufsize, "%-8s %d, %s", mn, insn->imm, reg_names[insn->reg2]);
        }
        break;

    case FMT_III: {
        uint32_t target = insn->addr + insn->imm;
        snprintf(buf, bufsize, "%-8s 0x%08X", mn, target);
        break;
    }

    case FMT_IV: {
        uint32_t target = insn->addr + insn->imm;
        snprintf(buf, bufsize, "%-8s 0x%08X", mn, target);
        break;
    }

    case FMT_V:
        snprintf(buf, bufsize, "%-8s 0x%04X, %s, %s", mn,
                 (uint16_t)insn->imm, reg_names[insn->reg1], reg_names[insn->reg2]);
        break;

    case FMT_VI:
        if (insn->opcode >= 0x34 && insn->opcode <= 0x37) {
            /* Store: reg2 is source */
            snprintf(buf, bufsize, "%-8s %s, %d[%s]", mn,
                     reg_names[insn->reg2], insn->imm, reg_names[insn->reg1]);
        } else {
            /* Load / IN / CAXI: reg2 is dest */
            snprintf(buf, bufsize, "%-8s %d[%s], %s", mn,
                     insn->imm, reg_names[insn->reg1], reg_names[insn->reg2]);
        }
        break;

    case FMT_VII:
        snprintf(buf, bufsize, "%-8s %s, %s", mn,
                 reg_names[insn->reg1], reg_names[insn->reg2]);
        break;
    }
}
