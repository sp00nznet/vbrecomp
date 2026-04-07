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

    ctx->max_resolved = 256;
    ctx->resolved_jumps = calloc(ctx->max_resolved, sizeof(ctx->resolved_jumps[0]));
    ctx->num_resolved = 0;

    ctx->max_jump_tables = 64;
    ctx->jump_tables = calloc(ctx->max_jump_tables, sizeof(ctx->jump_tables[0]));
    ctx->num_jump_tables = 0;
}

void v810_ctx_free(v810_ctx_t *ctx) {
    free(ctx->funcs);
    free(ctx->code_map);
    free(ctx->resolved_jumps);
    for (int i = 0; i < ctx->num_jump_tables; i++) {
        free(ctx->jump_tables[i].targets);
        free(ctx->jump_tables[i].raw_targets);
    }
    free(ctx->jump_tables);
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

    /* Normalize mirrored ROM addresses to canonical range.
     * VB ROM is mirrored throughout 0x07000000-0x07FFFFFF.
     * High addresses (0xFFF00000+) also map into ROM. */
    if (addr >= 0xFFF00000) {
        addr = addr & 0x07FFFFFF;
    }
    if (addr >= ROM_REGION_BASE && addr < ctx->rom_base) {
        uint32_t offset = (addr - ROM_REGION_BASE) % ctx->rom_size;
        addr = ctx->rom_base + offset;
    }

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

/* Normalize a ROM address by resolving mirrors to the canonical range.
 * VB ROM is mirrored throughout 0x07000000-0x07FFFFFF and at 0xFFF00000+. */
static uint32_t normalize_rom_addr(v810_ctx_t *ctx, uint32_t addr) {
    if (addr >= 0xFFF00000) {
        addr = addr & 0x07FFFFFF;
    }
    if (addr >= ROM_REGION_BASE && addr < ctx->rom_base) {
        uint32_t offset = (addr - ROM_REGION_BASE) % ctx->rom_size;
        addr = ctx->rom_base + offset;
    }
    return addr;
}

/* Check if an address is a valid ROM code address */
static bool is_rom_addr(v810_ctx_t *ctx, uint32_t addr) {
    addr = normalize_rom_addr(ctx, addr);
    return addr >= ctx->rom_base && addr < ROM_REGION_END;
}

/* Convert any valid code address to ROM offset */
static uint32_t addr_to_offset(v810_ctx_t *ctx, uint32_t addr) {
    addr = normalize_rom_addr(ctx, addr);
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

    /* Simple constant propagation: track known register values */
    uint32_t reg_val[32];
    bool reg_known[32];

    /* Branch target worklist for intraprocedural branches */
    uint32_t branch_targets[1024];
    int num_targets = 0;
    bool *visited_offsets = calloc(ctx->rom_size, sizeof(bool));

    /* Add the entry point */
    branch_targets[num_targets++] = offset;

    while (num_targets > 0) {
        /* Pop a target */
        offset = branch_targets[--num_targets];

        /* Reset register knowledge at each path start */
        memset(reg_known, 0, sizeof(reg_known));
        reg_val[0] = 0; reg_known[0] = true; /* r0 is always 0 */

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
                if (next_addr - func->addr > 0x10000) {
                    goto next_path;
                }
                func->end_addr = next_addr;
            }

            /* Update constant propagation */
            switch (insn.opcode) {
            case 0x00: /* MOV reg1, reg2 */
                if (insn.reg2 != 0 && reg_known[insn.reg1]) {
                    reg_val[insn.reg2] = reg_val[insn.reg1];
                    reg_known[insn.reg2] = true;
                } else if (insn.reg2 != 0) {
                    reg_known[insn.reg2] = false;
                }
                break;
            case 0x10: /* MOV imm5, reg2 */
                if (insn.reg2 != 0) {
                    reg_val[insn.reg2] = (uint32_t)insn.imm;
                    reg_known[insn.reg2] = true;
                }
                break;
            case 0x28: /* MOVEA imm16, reg1, reg2 */
                if (insn.reg2 != 0 && reg_known[insn.reg1]) {
                    reg_val[insn.reg2] = reg_val[insn.reg1] + (uint32_t)(int32_t)(int16_t)insn.imm;
                    reg_known[insn.reg2] = true;
                } else if (insn.reg2 != 0) {
                    reg_known[insn.reg2] = false;
                }
                break;
            case 0x2F: /* MOVHI imm16, reg1, reg2 */
                if (insn.reg2 != 0 && reg_known[insn.reg1]) {
                    reg_val[insn.reg2] = reg_val[insn.reg1] + ((uint32_t)(uint16_t)insn.imm << 16);
                    reg_known[insn.reg2] = true;
                } else if (insn.reg2 != 0) {
                    reg_known[insn.reg2] = false;
                }
                break;
            case 0x2B: /* JAL - clobbers r31 */
                reg_known[31] = false;
                /* Other registers may be clobbered by callee, but
                 * for simple constant prop we'll be conservative later */
                break;
            default:
                /* Any other instruction writing to reg2 invalidates it */
                if (insn.format == FMT_I || insn.format == FMT_II ||
                    insn.format == FMT_V || insn.format == FMT_VI ||
                    insn.format == FMT_VII) {
                    if (insn.reg2 != 0) reg_known[insn.reg2] = false;
                }
                break;
            }

            /* Analyze control flow */
            switch (insn.opcode) {
            case 0x06: /* JMP [reg1] - indirect jump */
                if (insn.reg1 == 31) {
                    /* JMP [r31] = return from subroutine */
                    goto next_path;
                }
                /* Try to resolve via constant propagation */
                if (reg_known[insn.reg1]) {
                    uint32_t target = reg_val[insn.reg1] & 0xFFFFFFFE;
                    if (target >= 0xFFF00000) target &= 0x07FFFFFF;
                    if (is_rom_addr(ctx, target)) {
                        uint32_t target_off = addr_to_offset(ctx, target);
                        printf("  Resolved JMP [r%d] at 0x%08X -> 0x%08X\n",
                               insn.reg1, insn.addr, target);
                        /* Record the resolution for the emitter */
                        if (ctx->num_resolved >= ctx->max_resolved) {
                            ctx->max_resolved *= 2;
                            ctx->resolved_jumps = realloc(ctx->resolved_jumps,
                                ctx->max_resolved * sizeof(ctx->resolved_jumps[0]));
                        }
                        ctx->resolved_jumps[ctx->num_resolved].from_addr = insn.addr;
                        ctx->resolved_jumps[ctx->num_resolved].to_addr = target;
                        ctx->num_resolved++;
                        /* Treat as intraprocedural jump or tail call */
                        if (target_off < ctx->rom_size && !visited_offsets[target_off]) {
                            if (num_targets < 1024) {
                                branch_targets[num_targets++] = target_off;
                            }
                        }
                    }
                }
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

    /* Scan confirmed functions for jump tables:
     * Pattern: SHL 2, rX / MOVHI imm, rX, rY / LD.W disp[rY], rZ / JMP [rZ]
     * The table address is (imm<<16) + disp, entries are 32-bit target addresses. */
    {
        for (int fi = 0; fi < ctx->num_funcs; fi++) {
            if (!ctx->funcs[fi].visited) continue;
            if (ctx->funcs[fi].addr < ctx->rom_base) continue;
            uint32_t off = addr_to_offset(ctx, ctx->funcs[fi].addr);
            uint32_t end = addr_to_offset(ctx, ctx->funcs[fi].end_addr);
            if (off >= ctx->rom_size || end > ctx->rom_size) continue;

            /* Look for LD.W + JMP pattern near unresolved indirect jumps */
            while (off + 6 <= end && off + 6 <= ctx->rom_size) {
                v810_insn_t insns[2];
                if (!v810_decode(ctx->rom, off, ctx->rom_size, &insns[0])) break;
                insns[0].addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);

                /* Check for LD.W disp[r], rZ followed by JMP [rZ] */
                if (insns[0].opcode == 0x33 && /* LD.W */
                    off + insns[0].size + 2 <= ctx->rom_size) {

                    uint32_t next_off = off + insns[0].size;
                    if (!v810_decode(ctx->rom, next_off, ctx->rom_size, &insns[1])) {
                        off += insns[0].size;
                        continue;
                    }
                    insns[1].addr = ROM_OFF_TO_ADDR(next_off, ctx->rom_base);

                    if (insns[1].opcode == 0x06 && /* JMP [reg] */
                        insns[1].reg1 == insns[0].reg2 && /* Same register */
                        insns[1].reg1 != 31) {

                        /* We found a LD.W + JMP pattern!
                         * Now look backwards for MOVHI to compute table base.
                         * Walk back up to 10 instructions to find MOVHI. */
                        int32_t disp = insns[0].imm;
                        uint8_t base_reg = insns[0].reg1;
                        uint32_t table_base = 0;
                        bool found_base = false;

                        /* Scan backwards for MOVHI writing to base_reg */
                        uint32_t scan = off > 40 ? off - 40 : 0;
                        while (scan < off) {
                            v810_insn_t prev;
                            if (!v810_decode(ctx->rom, scan, ctx->rom_size, &prev)) break;
                            if (prev.opcode == 0x2F && prev.reg2 == base_reg) {
                                /* MOVHI imm, regS, base_reg */
                                uint32_t hi = (uint32_t)(uint16_t)prev.imm << 16;
                                /* Check if regS is known (usually r0 or from SHL) */
                                table_base = hi + (uint32_t)disp;
                                found_base = true;
                            }
                            scan += prev.size;
                        }

                        if (found_base) {
                            /* Convert table address to ROM offset.
                             * The table base has the SHL'd index baked in at runtime,
                             * but for table_base we use the base without index (index=0). */
                            uint32_t tbl_cpu = table_base;
                            if (tbl_cpu >= 0xFFF00000) tbl_cpu &= 0x07FFFFFF;

                            if (tbl_cpu >= ctx->rom_base && tbl_cpu < ROM_REGION_END) {
                                uint32_t tbl_off = tbl_cpu - ctx->rom_base;

                                /* Read entries until we hit an invalid address */
                                int max_entries = 64;
                                uint32_t *targets = malloc(max_entries * sizeof(uint32_t));
                                uint32_t *raw_targets = malloc(max_entries * sizeof(uint32_t));
                                int n = 0;

                                for (int e = 0; e < max_entries; e++) {
                                    if (tbl_off + e * 4 + 4 > ctx->rom_size) break;
                                    const uint8_t *ep = ctx->rom + tbl_off + e * 4;
                                    uint32_t entry = ((uint16_t)ep[0] | ((uint16_t)ep[1] << 8))
                                                   | (((uint32_t)ep[2] | ((uint32_t)ep[3] << 8)) << 16);
                                    uint32_t mapped = entry;
                                    if (mapped >= 0xFFF00000) mapped &= 0x07FFFFFF;
                                    if (mapped < ctx->rom_base || mapped >= ROM_REGION_END) break;
                                    if (mapped & 1) break; /* Unaligned = end of table */
                                    raw_targets[n] = entry;
                                    targets[n] = mapped;
                                    n++;
                                }

                                if (n > 0) {
                                    printf("  Jump table at 0x%08X (ROM 0x%05X): %d entries from JMP at 0x%08X\n",
                                           tbl_cpu, tbl_off, n, insns[1].addr);

                                    /* Store jump table */
                                    if (ctx->num_jump_tables >= ctx->max_jump_tables) {
                                        ctx->max_jump_tables *= 2;
                                        ctx->jump_tables = realloc(ctx->jump_tables,
                                            ctx->max_jump_tables * sizeof(ctx->jump_tables[0]));
                                    }
                                    int jti = ctx->num_jump_tables++;
                                    ctx->jump_tables[jti].jmp_addr = insns[1].addr;
                                    ctx->jump_tables[jti].table_addr = tbl_cpu;
                                    ctx->jump_tables[jti].num_entries = n;
                                    ctx->jump_tables[jti].targets = targets;
                                    ctx->jump_tables[jti].raw_targets = raw_targets;

                                    /* Add each target as a function and mark confirmed */
                                    for (int e = 0; e < n; e++) {
                                        int idx = v810_ctx_add_func(ctx, targets[e], false, -1);
                                        if (idx >= 0 && ctx->funcs[fi].confirmed) {
                                            ctx->funcs[idx].confirmed = true;
                                        }
                                    }
                                } else {
                                    free(targets);
                                    free(raw_targets);
                                }
                            }
                        }
                    }
                }
                off += insns[0].size;
            }
        }

        /* Re-analyze newly discovered functions */
        progress = true;
        while (progress) {
            progress = false;
            for (int i = 0; i < ctx->num_funcs; i++) {
                if (!ctx->funcs[i].visited) {
                    analyze_func(ctx, i);
                    progress = true;
                }
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
