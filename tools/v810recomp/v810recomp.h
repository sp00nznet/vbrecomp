/*
 * v810recomp - V810 static recompiler
 * SPDX-License-Identifier: MIT
 *
 * Translates V810 ROM binary into C source that links against vbrecomp.
 */

#ifndef V810RECOMP_H
#define V810RECOMP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ROM mapping: VB ROMs sit at the top of 0x07000000-0x07FFFFFF */
#define ROM_REGION_BASE  0x07000000
#define ROM_REGION_END   0x08000000

/* Maximum ROM size: 16MB (realistic max is 2MB) */
#define MAX_ROM_SIZE     (16 * 1024 * 1024)

/* Address conversion */
#define ADDR_TO_ROM_OFF(addr, rom_base) ((addr) - (rom_base))
#define ROM_OFF_TO_ADDR(off, rom_base)  ((off) + (rom_base))

/* V810 instruction formats */
typedef enum {
    FMT_I,      /* reg1, reg2 (16-bit) */
    FMT_II,     /* imm5, reg2 (16-bit) */
    FMT_III,    /* Bcond disp9 (16-bit) */
    FMT_IV,     /* JR/JAL disp26 (32-bit) */
    FMT_V,      /* imm16, reg1, reg2 (32-bit) */
    FMT_VI,     /* load/store disp16[reg1], reg2 (32-bit) */
    FMT_VII,    /* subopcode (32-bit) */
} v810_format_t;

/* Decoded instruction */
typedef struct {
    uint32_t addr;          /* Address of this instruction */
    uint8_t  opcode;        /* Primary opcode (6 bits) */
    uint8_t  reg1;          /* Source register (5 bits) */
    uint8_t  reg2;          /* Destination register (5 bits) */
    uint8_t  subop;         /* Sub-opcode for Format VII */
    uint8_t  cond;          /* Condition code for Bcond */
    int32_t  imm;           /* Immediate/displacement value */
    uint16_t hw1, hw2;      /* Raw halfwords */
    v810_format_t format;   /* Instruction format */
    int      size;          /* 2 or 4 bytes */
} v810_insn_t;

/* Function entry discovered during analysis */
typedef struct {
    uint32_t addr;          /* Entry address */
    uint32_t end_addr;      /* Address after last instruction */
    bool     visited;       /* Already analyzed? */
    bool     is_interrupt;  /* Interrupt handler? */
    int      int_level;     /* Which interrupt level (-1 if not) */
} v810_func_t;

/* Analysis context */
typedef struct {
    const uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_base;      /* CPU address of ROM start */

    /* Function table */
    v810_func_t *funcs;
    int num_funcs;
    int max_funcs;

    /* Byte classification: which bytes are code vs data */
    uint8_t *code_map;      /* 1 byte per ROM byte: 'C'=code, 'D'=data, 0=unknown */
} v810_ctx_t;

/* Decode a single instruction */
bool v810_decode(const uint8_t *rom, uint32_t offset, uint32_t rom_size, v810_insn_t *out);

/* Get instruction mnemonic string */
const char *v810_mnemonic(const v810_insn_t *insn);

/* Disassemble to text */
void v810_disasm(const v810_insn_t *insn, char *buf, int bufsize);

/* Initialize analysis context */
void v810_ctx_init(v810_ctx_t *ctx, const uint8_t *rom, uint32_t rom_size);
void v810_ctx_free(v810_ctx_t *ctx);

/* Add a function entry point */
int v810_ctx_add_func(v810_ctx_t *ctx, uint32_t addr, bool is_interrupt, int int_level);

/* Analyze: discover functions by following calls from known entry points */
void v810_analyze(v810_ctx_t *ctx);

/* Emit recompiled C code */
void v810_emit_c(v810_ctx_t *ctx, FILE *out);

/* Emit function declarations header */
void v810_emit_header(v810_ctx_t *ctx, FILE *out);

#endif /* V810RECOMP_H */
