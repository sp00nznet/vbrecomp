/*
 * v810recomp - V810 static recompiler
 * SPDX-License-Identifier: MIT
 *
 * Usage: v810recomp <rom.vb> [output_dir]
 *
 * Reads a Virtual Boy ROM, discovers functions via control flow analysis,
 * and emits C source files that link against vbrecomp.
 */

#include "v810recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read entire file into malloc'd buffer */
static uint8_t *read_file(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > MAX_ROM_SIZE) {
        fprintf(stderr, "Error: invalid ROM size %ld\n", size);
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Error: failed to read ROM\n");
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_size = (uint32_t)size;
    return buf;
}

/* Read 16-bit LE from buffer */
static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Print ROM header info */
static void print_header(const uint8_t *rom, uint32_t rom_size) {
    if (rom_size < 544) {
        printf("ROM too small for header\n");
        return;
    }

    const uint8_t *hdr = rom + rom_size - 544;
    char title[21] = {0};
    memcpy(title, hdr, 20);
    /* Trim trailing spaces */
    for (int i = 19; i >= 0 && title[i] == ' '; i--) title[i] = 0;

    char maker[3] = {0};
    memcpy(maker, hdr + 0x19, 2);

    char code[5] = {0};
    memcpy(code, hdr + 0x1B, 4);

    uint8_t version = hdr[0x1F];

    printf("ROM Header:\n");
    printf("  Title:   %s\n", title);
    printf("  Maker:   %s\n", maker);
    printf("  Code:    %s\n", code);
    printf("  Version: 1.%d\n", version);
    printf("  Size:    %u bytes (%u KB)\n", rom_size, rom_size / 1024);
}

/* Decode the reset vector to find entry point */
static uint32_t find_entry_point(const uint8_t *rom, uint32_t rom_size, uint32_t rom_base) {
    /*
     * Reset vector is at the last 16 bytes of ROM (maps to 0xFFFFFFF0).
     * Typical pattern: MOVHI + MOVEA + JMP to load and jump to entry.
     */
    uint32_t vec_off = rom_size - 16;
    v810_insn_t insns[4];
    uint32_t off = vec_off;
    int count = 0;

    while (off < rom_size && count < 4) {
        if (!v810_decode(rom, off, rom_size, &insns[count])) break;
        insns[count].addr = rom_base + off;
        off += insns[count].size;
        count++;
    }

    /* Look for MOVHI + MOVEA + JMP pattern */
    if (count >= 3 &&
        insns[0].opcode == 0x2F &&  /* MOVHI */
        insns[1].opcode == 0x28 &&  /* MOVEA */
        insns[2].opcode == 0x06) {  /* JMP */

        uint32_t base_reg = insns[0].reg2;
        uint32_t hi = (uint32_t)((uint16_t)insns[0].imm) << 16;
        uint32_t lo = (uint32_t)(int32_t)(int16_t)insns[1].imm;
        uint32_t entry = hi + lo;

        /* Resolve mirrored address to canonical ROM range */
        if (entry >= 0xFFF00000) {
            entry = entry & 0x07FFFFFF;
        }
        if (entry >= ROM_REGION_BASE && entry < rom_base) {
            entry = rom_base + (entry - ROM_REGION_BASE) % rom_size;
        }

        printf("Reset vector: MOVHI 0x%04X + MOVEA 0x%04X -> entry at 0x%08X\n",
               (uint16_t)insns[0].imm, (uint16_t)insns[1].imm, entry);
        return entry;
    }

    /* Fallback: check for JR instruction */
    if (count >= 1 && insns[0].opcode == 0x2A) {
        uint32_t entry = insns[0].addr + insns[0].imm;
        if (entry >= 0xFFF00000) entry &= 0x07FFFFFF;
        if (entry >= ROM_REGION_BASE && entry < rom_base) {
            entry = rom_base + (entry - ROM_REGION_BASE) % rom_size;
        }
        printf("Reset vector: JR -> entry at 0x%08X\n", entry);
        return entry;
    }

    fprintf(stderr, "Warning: could not decode reset vector, using default\n");
    return rom_base;
}

/* Decode interrupt handler stubs to find real handlers */
static void find_interrupt_handlers(v810_ctx_t *ctx) {
    /*
     * Interrupt vectors are near the end of ROM:
     *   0xFFFFFE00: Game Pad (level 0)
     *   0xFFFFFE10: Timer (level 1)
     *   0xFFFFFE20: Expansion (level 2)
     *   0xFFFFFE30: Communication (level 3)
     *   0xFFFFFE40: VIP (level 4)
     *
     * Each vector has 16 bytes. The typical stub pattern is:
     *   ADD -4, r3       (push stack)
     *   ST.W r11, 0[r3]  (save r11)
     *   MOVHI hi, r0, r11
     *   MOVEA lo, r11, r11
     *   JMP [r11]        (jump to real handler)
     *
     * But sometimes it's shorter (direct JR/JAL).
     */
    static const struct { uint32_t cpu_addr; int level; const char *name; } vectors[] = {
        { 0xFFFFFE00, 0, "Game Pad" },
        { 0xFFFFFE10, 1, "Timer" },
        { 0xFFFFFE20, 2, "Expansion" },
        { 0xFFFFFE30, 3, "Communication" },
        { 0xFFFFFE40, 4, "VIP" },
    };

    for (int v = 0; v < 5; v++) {
        /* Convert CPU address to ROM offset */
        uint32_t rom_off = (vectors[v].cpu_addr & 0x07FFFFFF) - ctx->rom_base;
        if (rom_off >= ctx->rom_size) continue;

        /* Check if the vector area is all 0xFF (unused) */
        bool all_ff = true;
        for (int i = 0; i < 16 && rom_off + i < ctx->rom_size; i++) {
            if (ctx->rom[rom_off + i] != 0xFF) { all_ff = false; break; }
        }
        if (all_ff) {
            printf("  IRQ %d (%s): unused\n", vectors[v].level, vectors[v].name);
            continue;
        }

        /* Decode instructions in the vector stub */
        v810_insn_t insns[6];
        uint32_t off = rom_off;
        int count = 0;
        while (off < rom_off + 16 && off < ctx->rom_size && count < 6) {
            if (!v810_decode(ctx->rom, off, ctx->rom_size, &insns[count])) break;
            insns[count].addr = ctx->rom_base + off;
            off += insns[count].size;
            count++;
        }

        /* Look for the target address */
        uint32_t target = 0;
        bool found = false;

        /* Pattern: ... MOVHI + MOVEA + JMP */
        for (int i = 0; i < count - 2; i++) {
            if (insns[i].opcode == 0x2F &&    /* MOVHI */
                insns[i+1].opcode == 0x28 &&  /* MOVEA */
                insns[i+2].opcode == 0x06) {  /* JMP */
                uint32_t hi = (uint32_t)((uint16_t)insns[i].imm) << 16;
                uint32_t lo = (uint32_t)(int32_t)(int16_t)insns[i+1].imm;
                target = hi + lo;
                if (target >= 0xFFF00000) target &= 0x07FFFFFF;
                found = true;
                break;
            }
        }

        /* Pattern: JR disp26 */
        if (!found) {
            for (int i = 0; i < count; i++) {
                if (insns[i].opcode == 0x2A) {
                    target = insns[i].addr + insns[i].imm;
                    if (target >= 0xFFF00000) target &= 0x07FFFFFF;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            printf("  IRQ %d (%s): handler at 0x%08X\n", vectors[v].level, vectors[v].name, target);
            v810_ctx_add_func(ctx, target, true, vectors[v].level);
        } else {
            printf("  IRQ %d (%s): could not decode stub\n", vectors[v].level, vectors[v].name);
        }
    }
}

/* Parse a hex address (with or without 0x prefix). */
static bool parse_hex_addr(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || *end != 0) return false;
    *out = (uint32_t)v;
    return true;
}

/* Read a hints file, applying entries to the context.
 * Format (one per line, '#' for comments):
 *   jmp <from_hex> <to_hex>     # resolve JMP [reg] at from_hex to to_hex
 *   entry <addr_hex>             # add a function entry point
 */
static void load_hints(v810_ctx_t *ctx, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    printf("Loading hints from %s\n", path);
    char line[256];
    int n_jmp = 0, n_entry = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;

        char tok[32], a1[32], a2[32];
        int n = sscanf(p, "%31s %31s %31s", tok, a1, a2);
        if (n < 2) continue;

        if (strcmp(tok, "jmp") == 0 && n == 3) {
            uint32_t from, to;
            if (!parse_hex_addr(a1, &from) || !parse_hex_addr(a2, &to)) continue;
            if (ctx->num_resolved >= ctx->max_resolved) {
                ctx->max_resolved *= 2;
                ctx->resolved_jumps = realloc(ctx->resolved_jumps,
                    ctx->max_resolved * sizeof(ctx->resolved_jumps[0]));
            }
            ctx->resolved_jumps[ctx->num_resolved].from_addr = from;
            ctx->resolved_jumps[ctx->num_resolved].to_addr = to;
            ctx->num_resolved++;
            int idx = v810_ctx_add_func(ctx, to, false, -1);
            if (idx >= 0) ctx->funcs[idx].confirmed = true;
            n_jmp++;
        } else if (strcmp(tok, "entry") == 0 && n >= 2) {
            uint32_t a;
            if (!parse_hex_addr(a1, &a)) continue;
            int idx = v810_ctx_add_func(ctx, a, false, -1);
            if (idx >= 0) ctx->funcs[idx].confirmed = true;
            n_entry++;
        }
    }
    fclose(f);
    printf("  hints applied: %d jmp, %d entry\n", n_jmp, n_entry);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: v810recomp <rom.vb> [output_dir] [--hints <file>]\n");
        return 1;
    }

    const char *rom_path = argv[1];
    const char *out_dir = ".";
    const char *hints_path = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--hints") == 0 && i + 1 < argc) {
            hints_path = argv[++i];
        } else if (argv[i][0] != '-') {
            out_dir = argv[i];
        }
    }

    /* Load ROM */
    uint32_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) return 1;

    print_header(rom, rom_size);

    /* Init context */
    v810_ctx_t ctx;
    v810_ctx_init(&ctx, rom, rom_size);

    printf("\nROM base address: 0x%08X\n", ctx.rom_base);

    /* Find entry point */
    uint32_t entry = find_entry_point(rom, rom_size, ctx.rom_base);
    printf("Entry point: 0x%08X (ROM offset 0x%06X)\n\n", entry, entry - ctx.rom_base);
    {
        int idx = v810_ctx_add_func(&ctx, entry, false, -1);
        if (idx >= 0) ctx.funcs[idx].confirmed = true;
    }

    /* Find interrupt handlers */
    printf("Interrupt handlers:\n");
    find_interrupt_handlers(&ctx);

    /* Apply user hints (extra entry points + manual JMP-target resolutions) */
    if (hints_path) load_hints(&ctx, hints_path);
    /* Mark all interrupt handlers as confirmed */
    for (int i = 0; i < ctx.num_funcs; i++) {
        if (ctx.funcs[i].is_interrupt) ctx.funcs[i].confirmed = true;
    }
    printf("\n");

    /* Analyze */
    printf("Analyzing code...\n");
    fflush(stdout);
    v810_analyze(&ctx);
    fflush(stdout);

    printf("Emitting C code...\n");
    fflush(stdout);

    /* Emit C code */
    char path[512];

    snprintf(path, sizeof(path), "%s/recomp_funcs.c", out_dir);
    FILE *fc = fopen(path, "w");
    if (!fc) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", path);
        free(rom);
        v810_ctx_free(&ctx);
        return 1;
    }
    v810_emit_c(&ctx, fc);
    fclose(fc);
    printf("Wrote %s\n", path);

    snprintf(path, sizeof(path), "%s/recomp_funcs.h", out_dir);
    FILE *fh = fopen(path, "w");
    if (!fh) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", path);
        free(rom);
        v810_ctx_free(&ctx);
        return 1;
    }
    v810_emit_header(&ctx, fh);
    fclose(fh);
    printf("Wrote %s\n", path);

    v810_ctx_free(&ctx);
    free(rom);
    return 0;
}
