/*
 * v810recomp - Function discovery and analysis
 * SPDX-License-Identifier: MIT
 *
 * Discovers functions by following calls from known entry points.
 * Uses a worklist algorithm to find all reachable code.
 */

#include "v810recomp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void v810_ctx_init(v810_ctx_t *ctx, const uint8_t *rom, uint32_t rom_size) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->rom = rom;
    ctx->rom_size = rom_size;
    /* ROM is mapped at the top of 07 region */
    ctx->rom_base = ROM_REGION_END - rom_size;

    ctx->max_funcs = 4096;
    ctx->funcs = calloc(ctx->max_funcs, sizeof(v810_func_t));
    ctx->num_funcs = 0;

    ctx->code_map = calloc(rom_size, 1);
}

void v810_ctx_free(v810_ctx_t *ctx) {
    free(ctx->funcs);
    free(ctx->code_map);
    memset(ctx, 0, sizeof(*ctx));
}

/* Find function by address, returns index or -1 */
static int find_func(v810_ctx_t *ctx, uint32_t addr) {
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (ctx->funcs[i].addr == addr) return i;
    }
    return -1;
}

int v810_ctx_add_func(v810_ctx_t *ctx, uint32_t addr, bool is_interrupt, int int_level) {
    /* V810 instructions are 16-bit aligned */
    if (addr & 1) return -1;

    /* Already known? */
    int idx = find_func(ctx, addr);
    if (idx >= 0) return idx;

    /* Check if address is in ROM */
    if (addr < ctx->rom_base || addr >= ROM_REGION_END) {
        fprintf(stderr, "warning: function at 0x%08X outside ROM range\n", addr);
        return -1;
    }

    if (ctx->num_funcs >= ctx->max_funcs) {
        ctx->max_funcs *= 2;
        ctx->funcs = realloc(ctx->funcs, ctx->max_funcs * sizeof(v810_func_t));
    }

    idx = ctx->num_funcs++;
    ctx->funcs[idx].addr = addr;
    ctx->funcs[idx].end_addr = addr;
    ctx->funcs[idx].visited = false;
    ctx->funcs[idx].is_interrupt = is_interrupt;
    ctx->funcs[idx].confirmed = false;
    ctx->funcs[idx].int_level = int_level;

    return idx;
}

/* Check if an address is a valid ROM code address */
static bool is_rom_addr(v810_ctx_t *ctx, uint32_t addr) {
    /* Handle high addresses (0xFFF80000+) that map to ROM */
    if (addr >= 0xFFF00000) {
        /* These map into ROM via mirroring: addr & 0x07FFFFFF */
        uint32_t mapped = addr & 0x07FFFFFF;
        return mapped >= ctx->rom_base && mapped < ROM_REGION_END;
    }
    return addr >= ctx->rom_base && addr < ROM_REGION_END;
}

/* Convert any valid code address to ROM offset */
static uint32_t addr_to_offset(v810_ctx_t *ctx, uint32_t addr) {
    if (addr >= 0xFFF00000) {
        addr = addr & 0x07FFFFFF;
    }
    return addr - ctx->rom_base;
}

/* Analyze one function: trace instructions, find calls and branches */
static void analyze_func(v810_ctx_t *ctx, int func_idx) {
    v810_func_t *func = &ctx->funcs[func_idx];
    if (func->visited) return;
    func->visited = true;

    uint32_t addr = func->addr;
    if (!is_rom_addr(ctx, addr)) return;
    uint32_t offset = addr_to_offset(ctx, addr);
    if (offset >= ctx->rom_size) return;
    v810_insn_t insn;

    /* Branch target worklist for intraprocedural branches */
    uint32_t branch_targets[1024];
    int num_targets = 0;
    bool *visited_offsets = calloc(ctx->rom_size, sizeof(bool));

    /* Add the entry point */
    branch_targets[num_targets++] = offset;

    while (num_targets > 0) {
        /* Pop a target */
        offset = branch_targets[--num_targets];

        while (offset < ctx->rom_size) {
            if (visited_offsets[offset]) break;  /* Already traced this path */
            visited_offsets[offset] = true;

            if (!v810_decode(ctx->rom, offset, ctx->rom_size, &insn)) break;

            insn.addr = ROM_OFF_TO_ADDR(offset, ctx->rom_base);

            /* Mark bytes as code */
            for (int i = 0; i < insn.size; i++) {
                if (offset + i < ctx->rom_size) {
                    ctx->code_map[offset + i] = 'C';
                }
            }

            /* Track function extent, with size limit */
            uint32_t next_addr = insn.addr + insn.size;
            if (next_addr > func->end_addr) {
                /* Cap function size at 16KB to prevent runaway analysis */
                if (next_addr - func->addr > 0x4000) {
                    goto next_path;
                }
                func->end_addr = next_addr;
            }

            /* Analyze control flow */
            switch (insn.opcode) {
            case 0x06: /* JMP [reg1] - indirect jump */
                if (insn.reg1 == 31) {
                    /* JMP [r31] = return from subroutine */
                    goto next_path;
                }
                /* Other indirect jumps: could be switch tables, etc. */
                /* For now, stop tracing this path */
                goto next_path;

            case 0x2B: { /* JAL disp26 - call */
                uint32_t target = insn.addr + insn.imm;
                if (is_rom_addr(ctx, target)) {
                    int callee = v810_ctx_add_func(ctx, target, false, -1);
                    /* Propagate confirmed status */
                    if (callee >= 0 && func->confirmed) {
                        ctx->funcs[callee].confirmed = true;
                    }
                }
                /* Fall through to next instruction after call */
                break;
            }

            case 0x2A: { /* JR disp26 - unconditional jump */
                uint32_t target = insn.addr + insn.imm;
                if (is_rom_addr(ctx, target)) {
                    uint32_t target_off = addr_to_offset(ctx, target);
                    /* If target is within function or nearby, it's intraprocedural */
                    if (target_off < ctx->rom_size && !visited_offsets[target_off]) {
                        if (num_targets < 1024) {
                            branch_targets[num_targets++] = target_off;
                        }
                    }
                }
                goto next_path;
            }

            /* Bcond - conditional branches */
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x24: case 0x25: case 0x26: case 0x27: {
                if (insn.cond == 0x05) {
                    /* BR (always) - unconditional */
                    uint32_t target = insn.addr + insn.imm;
                    if (is_rom_addr(ctx, target)) {
                        uint32_t target_off = addr_to_offset(ctx, target);
                        if (target_off < ctx->rom_size && !visited_offsets[target_off]) {
                            if (num_targets < 1024) {
                                branch_targets[num_targets++] = target_off;
                            }
                        }
                    }
                    goto next_path;
                } else if (insn.cond == 0x0D) {
                    /* NOP (never branch) - fall through */
                    break;
                } else {
                    /* Conditional: trace both paths */
                    uint32_t target = insn.addr + insn.imm;
                    if (is_rom_addr(ctx, target)) {
                        uint32_t target_off = addr_to_offset(ctx, target);
                        if (target_off < ctx->rom_size && !visited_offsets[target_off]) {
                            if (num_targets < 1024) {
                                branch_targets[num_targets++] = target_off;
                            }
                        }
                    }
                    /* Fall through */
                    break;
                }
            }

            case 0x19: /* RETI */
                goto next_path;

            case 0x1A: /* HALT */
                goto next_path;

            default:
                break;
            }

            offset += insn.size;
            continue;

        next_path:
            offset += insn.size;
            break;
        }
    }

    free(visited_offsets);
}

/* Compare function entries by address for sorting */
static int func_cmp(const void *a, const void *b) {
    const v810_func_t *fa = a, *fb = b;
    if (fa->addr < fb->addr) return -1;
    if (fa->addr > fb->addr) return 1;
    return 0;
}

/* Brute-force scan: find all JAL targets in the entire ROM */
static void scan_all_jal_targets(v810_ctx_t *ctx) {
    int found = 0;
    for (uint32_t off = 0; off + 4 <= ctx->rom_size; off += 2) {
        uint16_t hw1 = (uint16_t)ctx->rom[off] | ((uint16_t)ctx->rom[off + 1] << 8);
        uint8_t opcode = (hw1 >> 10) & 0x3F;

        if (opcode == 0x2B) {  /* JAL */
            uint16_t hw2 = (uint16_t)ctx->rom[off + 2] | ((uint16_t)ctx->rom[off + 3] << 8);
            uint32_t raw = ((uint32_t)(hw1 & 0x3FF) << 16) | hw2;
            int32_t disp = (int32_t)((raw ^ (1u << 25)) - (1u << 25));
            uint32_t addr = ctx->rom_base + off;
            uint32_t target = addr + disp;

            if (target >= 0xFFF00000) target &= 0x07FFFFFF;

            if (target >= ctx->rom_base && target < ROM_REGION_END && (target & 1) == 0) {
                if (find_func(ctx, target) < 0) {
                    v810_ctx_add_func(ctx, target, false, -1);
                    found++;
                }
            }
        }
    }
    printf("JAL scan: found %d additional function targets\n", found);
}

void v810_analyze(v810_ctx_t *ctx) {
    /* First: brute-force scan for all JAL call targets in the ROM */
    scan_all_jal_targets(ctx);

    /* Process worklist until no unvisited functions remain */
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (!ctx->funcs[i].visited) {
                analyze_func(ctx, i);
                progress = true;
            }
        }
    }

    /* Sort functions by address */
    qsort(ctx->funcs, ctx->num_funcs, sizeof(v810_func_t), func_cmp);

    /* Propagate confirmed status: re-analyze confirmed functions
     * to mark their callees as confirmed too */
    {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < ctx->num_funcs; i++) {
                if (!ctx->funcs[i].confirmed) continue;
                /* Re-scan this function's code for JAL targets */
                uint32_t off = addr_to_offset(ctx, ctx->funcs[i].addr);
                uint32_t end = addr_to_offset(ctx, ctx->funcs[i].end_addr);
                if (off >= ctx->rom_size || end > ctx->rom_size) continue;
                while (off < end && off < ctx->rom_size) {
                    v810_insn_t insn;
                    if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) break;
                    insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);
                    if (insn.opcode == 0x2B) { /* JAL */
                        uint32_t target = insn.addr + insn.imm;
                        if (target >= 0xFFF00000) target &= 0x07FFFFFF;
                        int idx = find_func(ctx, target);
                        if (idx >= 0 && !ctx->funcs[idx].confirmed) {
                            ctx->funcs[idx].confirmed = true;
                            changed = true;
                        }
                    }
                    off += insn.size;
                }
            }
        }
    }

    /* Count confirmed vs total */
    int confirmed = 0;
    for (int i = 0; i < ctx->num_funcs; i++) {
        if (ctx->funcs[i].confirmed) confirmed++;
    }

    printf("Confirmed functions: %d / %d\n", confirmed, ctx->num_funcs);

    /* Count code bytes */
    int code_bytes = 0;
    for (uint32_t i = 0; i < ctx->rom_size; i++) {
        if (ctx->code_map[i] == 'C') code_bytes++;
    }

    printf("Analysis complete: %d functions found\n", ctx->num_funcs);
    printf("Code coverage: %d / %u bytes (%.1f%%)\n",
           code_bytes, ctx->rom_size, 100.0 * code_bytes / ctx->rom_size);

    /* Only print first/last few functions to avoid flooding */
    int show = ctx->num_funcs < 20 ? ctx->num_funcs : 10;
    for (int i = 0; i < show; i++) {
        printf("  func_%08X  [%08X - %08X]%s\n",
               ctx->funcs[i].addr,
               ctx->funcs[i].addr,
               ctx->funcs[i].end_addr,
               ctx->funcs[i].is_interrupt ? " (interrupt)" : "");
    }
    if (ctx->num_funcs > 20) {
        printf("  ... (%d more) ...\n", ctx->num_funcs - 20);
        for (int i = ctx->num_funcs - 10; i < ctx->num_funcs; i++) {
            printf("  func_%08X  [%08X - %08X]%s\n",
                   ctx->funcs[i].addr,
                   ctx->funcs[i].addr,
                   ctx->funcs[i].end_addr,
                   ctx->funcs[i].is_interrupt ? " (interrupt)" : "");
        }
    }
}
