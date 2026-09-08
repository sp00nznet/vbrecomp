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

/* Bounded split budget; see split_func_at. */
static int split_count;

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
    ctx->byte_owner = calloc(rom_size, sizeof(uint32_t));
    split_count = 0;
    ctx->visit_stamp = calloc(rom_size, sizeof(uint32_t));
    ctx->visit_gen = 0;

    ctx->max_resolved = 256;
    ctx->resolved_jumps = calloc(ctx->max_resolved, sizeof(ctx->resolved_jumps[0]));
    ctx->num_resolved = 0;

    ctx->max_jump_tables = 64;
    ctx->jump_tables = calloc(ctx->max_jump_tables, sizeof(ctx->jump_tables[0]));
    ctx->num_jump_tables = 0;

    ctx->max_skip_funcs = 16;
    ctx->skip_funcs = calloc(ctx->max_skip_funcs, sizeof(ctx->skip_funcs[0]));
    ctx->num_skip_funcs = 0;

    ctx->max_rename_funcs = 16;
    ctx->rename_funcs = calloc(ctx->max_rename_funcs, sizeof(ctx->rename_funcs[0]));
    ctx->num_rename_funcs = 0;
}

bool v810_is_renamed(v810_ctx_t *ctx, uint32_t addr) {
    addr = v810_normalize_rom_addr(ctx, addr);
    for (int i = 0; i < ctx->num_rename_funcs; i++) {
        if (ctx->rename_funcs[i] == addr) return true;
    }
    return false;
}

uint32_t v810_get_skip_bytes(v810_ctx_t *ctx, uint32_t target) {
    for (int i = 0; i < ctx->num_skip_funcs; i++) {
        if (ctx->skip_funcs[i].target == target) return ctx->skip_funcs[i].skip_bytes;
    }
    return 0;
}

void v810_ctx_free(v810_ctx_t *ctx) {
    free(ctx->funcs);
    free(ctx->code_map);
    free(ctx->byte_owner);
    free(ctx->visit_stamp);
    free(ctx->resolved_jumps);
    for (int i = 0; i < ctx->num_jump_tables; i++) {
        free(ctx->jump_tables[i].targets);
        free(ctx->jump_tables[i].raw_targets);
    }
    free(ctx->jump_tables);
    free(ctx->skip_funcs);
    free(ctx->rename_funcs);
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
     * High addresses (>=0x08000000) are mirrors of the 27-bit physical map. */
    addr = v810_normalize_rom_addr(ctx, addr);

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
 * VB ROM is mirrored throughout 0x07000000-0x07FFFFFF; the whole 32-bit space mirrors the 27-bit physical map every 0x08000000. */
uint32_t v810_normalize_rom_addr(v810_ctx_t *ctx, uint32_t addr) {
    if (addr >= 0x08000000) {
        /* A jal that runs off the top of the ROM window wraps within the CART,
         * which repeats every rom_size bytes -- not to the bottom of the 27-bit
         * map. Hyper Fighting calls 0x08000290 from 0x07C20906: exactly one
         * mirror period past a 4 MB ROM based at 0x07C00000, i.e. 0x07C00290.
         * Masking to 0x07FFFFFF gave 0x00000290, an address inside VIP memory,
         * and the call was dropped on the floor -- seven of them in that one
         * function. Only prefer the cart wrap when the plain mask does NOT
         * land in the ROM region, so ordinary mirrors keep their old meaning. */
        uint32_t masked = addr & 0x07FFFFFF;
        if (masked < ROM_REGION_BASE && addr >= ctx->rom_base && ctx->rom_size) {
            addr = ctx->rom_base + ((addr - ctx->rom_base) % ctx->rom_size);
        } else {
            addr = masked;
        }
    }
    if (addr >= ROM_REGION_BASE && addr < ctx->rom_base) {
        uint32_t offset = (addr - ROM_REGION_BASE) % ctx->rom_size;
        addr = ctx->rom_base + offset;
    }
    return addr;
}

/* Check if an address is a valid ROM code address */
static bool is_rom_addr(v810_ctx_t *ctx, uint32_t addr) {
    addr = v810_normalize_rom_addr(ctx, addr);
    return addr >= ctx->rom_base && addr < ROM_REGION_END;
}

/* Convert any valid code address to ROM offset */
static uint32_t addr_to_offset(v810_ctx_t *ctx, uint32_t addr) {
    addr = v810_normalize_rom_addr(ctx, addr);
    return addr - ctx->rom_base;
}

/* NOTE: a branch target BELOW a function's entry cannot be emitted. A function
 * is written out as one contiguous [addr, end_addr) range whose end only grows
 * FORWARDS, so analysis traces such a target and the emitter then drops it,
 * falling back to a bare `return`. The standard V810 boot idiom -- `jmp` to the
 * loop test with the body just above -- lands on it.
 *
 * Pulling the entry back over the body DOES fix that specific truncation
 * (Mario's Tennis `blt 0x07F8013E` becomes a goto and its WRAM clear survives),
 * but measured across the corpus it unblocked nothing and cost one game: Mario
 * Clash stopped advancing. It is removed rather than kept on principle. If it
 * is tried again, measure `advancing`, not `renders`, and check Mario Clash --
 * and note that the two obvious in-traversal forms are both wrong (registering
 * each backward target as a function cascades on every loop back edge, 56
 * functions to 17,892 on Elevated Speed; mutating func->addr mid-trace
 * segfaults). A settled post-pass over the function table is the workable
 * shape. See docs/RENDER_TRIAGE.md.
 */


/* Can this function be cut at `at` without turning a branch into a transfer?
 * A branch that crosses the cut becomes a cross-function jump; if it is a loop
 * back edge the loop grows the stack per iteration. A branch that lands exactly
 * ON the cut is fine -- that is the loop-head case, and the loop stays whole in
 * the second half. */
static bool safe_split_point(v810_ctx_t *ctx, const v810_func_t *f, uint32_t at) {
    if (at <= f->addr || at >= f->end_addr) return false;
    uint32_t off = addr_to_offset(ctx, f->addr);
    uint32_t end = addr_to_offset(ctx, f->end_addr);
    if (off >= ctx->rom_size || end > ctx->rom_size) return false;
    bool on_boundary = false;
    while (off < end && off < ctx->rom_size) {
        v810_insn_t insn;
        if (!v810_decode(ctx->rom, off, ctx->rom_size, &insn)) return false;
        insn.addr = ROM_OFF_TO_ADDR(off, ctx->rom_base);
        if (insn.addr == at) on_boundary = true;
        bool is_branch = (insn.format == FMT_III && insn.cond != 0x0D)
                      || insn.opcode == 0x2A;
        if (is_branch) {
            uint32_t t = insn.addr + insn.imm;
            if (is_rom_addr(ctx, t)) {
                t = v810_normalize_rom_addr(ctx, t);
                if (t > f->addr && t < f->end_addr) {
                    bool src_after = insn.addr >= at;
                    bool dst_after = t >= at;
                    if (src_after != dst_after) return false;  /* crosses */
                }
            }
        }
        off += insn.size;
        /* Inline arguments after a long call are DATA. Stepping over them is
         * what keeps the cut off a byte that only looks like an instruction:
         * Galactic Pinball split at 0x07F51728, the first argument word of a
         * `jal 0x07F417B8`, and 0x9800 there decodes as `bp .` -- a branch to
         * itself that the preceding function then fell into. */
        if (insn.opcode == 0x2B)
            off += v810_get_skip_bytes(ctx,
                       v810_normalize_rom_addr(ctx, insn.addr + insn.imm));
    }
    /* A cut that is not on an instruction boundary splits an instruction (or a
     * run of data) in half, and both halves then decode as garbage. */
    return on_boundary;
}

/* Cut `fi` at `at`: the tail becomes its own function, so a jump can land on it
 * without duplicating a single instruction. Returns false if the cut is unsafe
 * or the entry already exists. */
/* Splitting creates functions, each of which is analysed and can produce more
 * split points. On most ROMs that settles immediately; on SD Gundam it does
 * not, and the emitted file passed 13 MB with analysis still running after five
 * minutes. Cap it: past the limit the remaining jumps fall back to being
 * counted as unimplemented, which is where they were before any of this. */
/* One budget for every entry these passes add -- splits AND registrations.
 * Each new entry is analysed and can produce more targets; on SD Gundam that
 * does not settle, and analysis ran past two minutes with the emitted file over
 * 80 MB. Past the budget the remaining jumps fall back to being counted as
 * unimplemented, which is where they were before any of this. */
#define MAX_SPLITS 512

static bool split_func_at(v810_ctx_t *ctx, int fi, uint32_t at) {
    if (split_count >= MAX_SPLITS) return false;
    if (find_func(ctx, at) >= 0) return false;
    if (!safe_split_point(ctx, &ctx->funcs[fi], at)) return false;
    uint32_t old_end = ctx->funcs[fi].end_addr;
    int ni = v810_ctx_add_func(ctx, at, false, -1);
    if (ni < 0) return false;
    ctx->funcs[ni].confirmed = true;
    ctx->funcs[ni].visited = true;
    ctx->funcs[ni].end_addr = old_end;
    ctx->funcs[fi].end_addr = at;
    split_count++;
    return true;
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
    /* One generation per analyse pass; see visit_stamp in v810recomp.h. */
    uint32_t gen = ++ctx->visit_gen;
    uint32_t *visited_offsets = ctx->visit_stamp;
#define VISITED(off)      (visited_offsets[off] == gen)
#define MARK_VISITED(off) (visited_offsets[off] = gen)

    /* Add the entry point */
    branch_targets[num_targets++] = offset;

    while (num_targets > 0) {
        /* Pop a target */
        offset = branch_targets[--num_targets];

        /* Reset register knowledge at each path start */
        memset(reg_known, 0, sizeof(reg_known));
        reg_val[0] = 0; reg_known[0] = true; /* r0 is always 0 */

        while (offset < ctx->rom_size) {
            /* Re-acquire: adding a discovered callee below can realloc
             * ctx->funcs, which would leave a cached `func` dangling. */
            func = &ctx->funcs[func_idx];

            if (VISITED(offset)) break;  /* Already traced this path */
            MARK_VISITED(offset);

            if (!v810_decode(ctx->rom, offset, ctx->rom_size, &insn)) break;

            insn.addr = ROM_OFF_TO_ADDR(offset, ctx->rom_base);

            /* Mark bytes as code */
            for (int i = 0; i < insn.size; i++) {
                if (offset + i < ctx->rom_size) {
                    ctx->code_map[offset + i] = 'C';
                    /* Prefer the CONTAINER, not whoever got here first. An
                     * interior function analysed before the function that
                     * contains it would otherwise claim its own bytes and
                     * report as top-level -- Null Pointer Mockup's
                     * sub_07F93942 sits inside sub_07F93920 and was invisible
                     * to the interior-entry table for exactly that reason. The
                     * container starts earlier, so the lower address wins. */
                    if (!ctx->byte_owner[offset + i]
                        || func->addr < ctx->byte_owner[offset + i])
                        ctx->byte_owner[offset + i] = func->addr;
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
            case 0x11: /* ADD imm5, reg2 */
                if (insn.reg2 != 0) {
                    if (reg_known[insn.reg2]) reg_val[insn.reg2] += (uint32_t)insn.imm;
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
                /* ...except `jal .+4`, which is not a call at all: it is how the
                 * V810 captures the PC. The long-call idiom continues
                 *     jal .+4 ; add N, r31 ; jmp [rX]
                 * so r31 ends up holding the address the callee returns to, and
                 * knowing it is what lets the trace follow the return. */
                if ((uint32_t)(insn.addr + insn.imm) == insn.addr + (uint32_t)insn.size) {
                    reg_val[31] = insn.addr + (uint32_t)insn.size;
                    reg_known[31] = true;
                    break;
                }
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
                if (insn.reg1 == 31 && !reg_known[31]) {
                    /* jmp [r31] with an unknown r31 is a subroutine return.
                     * But if r31 holds a constant we propagated, it's a
                     * computed jump -- a common reset-vector idiom loads r31
                     * (movhi+movea) and jmps to the entry, which would
                     * otherwise be mistaken for a return (dead boot, SP=0). */
                    goto next_path;
                }
                /* Try to resolve via constant propagation */
                if (reg_known[insn.reg1]) {
                    uint32_t target = reg_val[insn.reg1] & 0xFFFFFFFE;
                    target = v810_normalize_rom_addr(ctx, target);
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
                        /* Store the canonical (mirror-normalized) target so the
                         * emitter's find_func_by_addr matches the function head,
                         * which add_func also keys at the normalized address.
                         * A reset vector built with `movhi 0x0700` yields a low
                         * mirror (e.g. 0x07001688) for an entry keyed at the top
                         * mirror (0x07F01688); without this the emitter saw "no
                         * local label" and emitted a bare return -> entry never ran. */
                        ctx->resolved_jumps[ctx->num_resolved].to_addr =
                            v810_normalize_rom_addr(ctx, target);
                        ctx->num_resolved++;
                        /* A jmp [reg] never returns to this flow, so the
                         * target is the head of its own routine. Promote it
                         * to a function and let the emitter tail-call it
                         * (same handling as JAL callees and `jmp` hints).
                         * Folding it into the current function instead would
                         * create overlapping function ranges, which produce
                         * gotos to labels defined only in the other function
                         * (undefined-label compile errors). */
                        (void)target_off;
                        bool here_confirmed = func->confirmed;
                        int jtgt = v810_ctx_add_func(ctx, target, false, -1);
                        /* add_func may realloc ctx->funcs; refresh func. */
                        func = &ctx->funcs[func_idx];
                        if (jtgt >= 0 && here_confirmed) {
                            ctx->funcs[jtgt].confirmed = true;
                        }
                    }
                }
                /* A `jmp [rX]` whose r31 we know is a CALL, not a jump: the
                 * long-call idiom above set r31 to the address after this
                 * instruction, and the callee returns there. Without following
                 * it the trace stops at the jmp and the function's epilogue is
                 * emitted nowhere at all -- SPONG's sub_07E33038 loses the
                 * `ld.w 0[r3],r31 ; add 4,r3 ; jmp [r31]` at 0x07E33052, so every
                 * virtual method call in the game returns into a hole. */
                if (insn.reg1 != 31 && reg_known[31]) {
                    uint32_t ret = v810_normalize_rom_addr(ctx, reg_val[31] & ~1u);
                    if (is_rom_addr(ctx, ret)) {
                        uint32_t ret_off = addr_to_offset(ctx, ret);
                        if (ret_off < ctx->rom_size && num_targets < 1024)
                            branch_targets[num_targets++] = ret_off;
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
                /* Skip-after-JAL helpers consume N bytes of inline data
                 * after the call. Don't analyze those bytes as code. */
                uint32_t skip = v810_get_skip_bytes(ctx, target);
                if (skip > 0) {
                    /* Mark the skipped bytes as data and advance offset
                     * (insn.size is added by the loop's normal step). */
                    for (uint32_t i = 0; i < skip; i++) {
                        if (offset + insn.size + i < ctx->rom_size) {
                            ctx->code_map[offset + insn.size + i] = 'D';
                        }
                    }
                    offset += skip;
                }
                /* Fall through to next instruction after call */
                break;
            }

            case 0x2A: { /* JR disp26 - unconditional jump */
                uint32_t target = insn.addr + insn.imm;
                if (is_rom_addr(ctx, target)) {
                    uint32_t target_off = addr_to_offset(ctx, target);
                    /* If target is within function or nearby, it's intraprocedural */
                    if (target_off < ctx->rom_size && !VISITED(target_off)) {
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
                        if (target_off < ctx->rom_size && !VISITED(target_off)) {
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
                        if (target_off < ctx->rom_size && !VISITED(target_off)) {
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

#undef VISITED
#undef MARK_VISITED
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

            target = v810_normalize_rom_addr(ctx, target);

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

                /* Check for LD.W disp[r], rZ ... JMP [rZ]. The JMP need not be
                 * the very next instruction: a state-machine dispatch commonly
                 * loads the continuation into r31 (movhi/movea) between the
                 * table load and the jmp. Scan a short window forward for the
                 * JMP on the loaded register, stopping if that register is
                 * overwritten or control flow leaves first. */
                if (insns[0].opcode == 0x33 && /* LD.W */
                    off + insns[0].size + 2 <= ctx->rom_size) {

                    uint8_t tbl_reg = insns[0].reg2;
                    uint32_t joff = off + insns[0].size;
                    bool found_jmp = false;
                    for (int step = 0; step < 5 && joff + 2 <= ctx->rom_size; step++) {
                        v810_insn_t ji;
                        if (!v810_decode(ctx->rom, joff, ctx->rom_size, &ji)) break;
                        ji.addr = ROM_OFF_TO_ADDR(joff, ctx->rom_base);
                        if (ji.opcode == 0x06 && ji.reg1 == tbl_reg && ji.reg1 != 31) {
                            insns[1] = ji;
                            found_jmp = true;
                            break;
                        }
                        /* Stop at any control-flow change or a write that would
                         * clobber the table-target register. */
                        if (ji.opcode == 0x06 || ji.opcode == 0x2A ||
                            ji.opcode == 0x2B || ji.opcode == 0x04) break;
                        if (ji.reg2 == tbl_reg) break;
                        joff += ji.size;
                    }

                    if (found_jmp) {

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
                            /* Normalize ROM mirrors: the table base is often a
                             * low mirror (e.g. 0x0703DEE4 for a 512K ROM keyed
                             * at 0x07F80000). Without this it fails the range
                             * check and the whole table is missed. */
                            uint32_t tbl_cpu = v810_normalize_rom_addr(ctx, table_base);

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
                                    if (entry & 1) break; /* Unaligned = end of table */
                                    /* Normalize mirrors so low-mirror entries
                                     * (e.g. 0x07024C64) map to the canonical
                                     * function key (0x07FA4C64) instead of being
                                     * rejected as out-of-range. */
                                    uint32_t mapped = v810_normalize_rom_addr(ctx, entry);
                                    if (mapped < ctx->rom_base || mapped >= ROM_REGION_END) break;
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
        /* Each pass rescans every confirmed function's whole body, so the cost
         * is O(functions x code size) per pass. Confirmation settles in a few
         * passes on every ROM in the corpus; bounding it stops a ROM where it
         * does not (SD Gundam) from running for minutes. */
        bool changed = true;
        int passes = 0;
        while (changed && passes++ < 8) {
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
                    /* A JAL target is reachable, and so is a branch target:
                     * the emitter turns a branch to a known function into a
                     * direct call, and an UNCONFIRMED function is never
                     * declared, defined or stubbed -- so leaving these
                     * unconfirmed means the branch degrades to a bare `return`
                     * and the rest of the function is unreachable. Confirming
                     * them is what makes those edges real code. */
                    bool is_edge = (insn.opcode == 0x2B)   /* JAL */
                                || (insn.opcode == 0x2A)   /* JR  */
                                || (insn.format == FMT_III && insn.cond != 0x0D);
                    if (is_edge) {
                        uint32_t target = insn.addr + insn.imm;
                        target = v810_normalize_rom_addr(ctx, target);
                        int idx = find_func(ctx, target);
                        /* Target inside another function: CUT that function
                         * there rather than making an overlapping copy of its
                         * tail, and only where the cut is safe. */
                        if (idx < 0) {
                            for (int k = 0; k < ctx->num_funcs; k++) {
                                if (target > ctx->funcs[k].addr
                                 && target < ctx->funcs[k].end_addr) {
                                    if (split_func_at(ctx, k, target)) changed = true;
                                    break;
                                }
                            }
                            idx = find_func(ctx, target);
                        }
                        bool inside_any = false;
                        for (int k = 0; k < ctx->num_funcs && !inside_any; k++) {
                            if (target > ctx->funcs[k].addr
                             && target <  ctx->funcs[k].end_addr) inside_any = true;
                        }
                        if (idx < 0 && !inside_any
                                    && (target < ctx->funcs[i].addr
                                     || target >= ctx->funcs[i].end_addr)) {
                            /* A branch or jump that LEAVES this function needs
                             * an entry to land on: the emitter parks such a
                             * target for vb_recomp_drain(), which dispatches it
                             * by address, and that only works if the address is
                             * in the table. A backward target lands inside an
                             * existing function; the new entry overlaps it,
                             * costing duplicated code.
                             *
                             * Only when the target is not INSIDE some existing
                             * function, though. An overlapping duplicate turns
                             * that function's own loops into cross-function
                             * jumps: Mario Clash's counted loop at 0x07F000B2
                             * (jal, jal, add -1 r25, bnz back) got a second
                             * entry at 0x07F000BA, so the back edge became a
                             * transfer dispatched from a nested drain and the
                             * loop grew the stack one frame per iteration until
                             * it died. A depth trace named the pair. */
                            if (split_count < MAX_SPLITS) {
                                idx = v810_ctx_add_func(ctx, target, false, -1);
                                if (idx >= 0) { changed = true; split_count++; }
                            }
                        }
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

    /* How much of the jump-table graph confirmation actually reached. A table
     * whose targets stay unconfirmed is emitted as a set of do-nothing stubs,
     * i.e. a game state machine that silently does nothing when dispatched.
     * Jack Bros: 7 tables, NOT ONE in a confirmed function, 221 targets all
     * unconfirmed. Waterworld: 12 tables, 162 targets, all confirmed.
     *
     * Confirming them unconditionally is NOT the fix -- measured, it is +2
     * advancing but it destroys Space Squash (4,914 drawn pixels -> 4), which
     * is a net loss. Distinguishing a real dispatch table from bytes that
     * merely decode like one is the open problem; this line measures it. */
    {
        int owned = 0, unowned = 0, tconf = 0, tunconf = 0;
        for (int jt = 0; jt < ctx->num_jump_tables; jt++) {
            uint32_t ja = ctx->jump_tables[jt].jmp_addr;
            bool oc = false;
            for (int k = 0; k < ctx->num_funcs && !oc; k++)
                if (ja >= ctx->funcs[k].addr && ja < ctx->funcs[k].end_addr
                    && ctx->funcs[k].confirmed) oc = true;
            if (oc) owned++; else unowned++;
            for (int e = 0; e < ctx->jump_tables[jt].num_entries; e++) {
                int idx = find_func(ctx, ctx->jump_tables[jt].targets[e]);
                if (idx >= 0 && ctx->funcs[idx].confirmed) tconf++; else tunconf++;
            }
        }
        fprintf(stderr, "jump tables: %d (owner confirmed %d, not %d); "
                        "targets confirmed %d, not %d\n",
                ctx->num_jump_tables, owned, unowned, tconf, tunconf);
    }
    /* The confirm pass can add entries; analyse them or they emit as stubs. */
    {
        bool progress2 = true;
        while (progress2) {
            progress2 = false;
            for (int i = 0; i < ctx->num_funcs; i++) {
                if (!ctx->funcs[i].visited) { analyze_func(ctx, i); progress2 = true; }
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

    {   /* How much does exact ownership disagree with the end_addr range? */
        int by_range = 0, by_owner = 0;
        for (int i = 0; i < ctx->num_funcs; i++) {
            if (!ctx->funcs[i].confirmed) continue;
            uint32_t fa = ctx->funcs[i].addr;
            uint32_t off = addr_to_offset(ctx, fa);
            bool rng = false;
            for (int k = 0; k < ctx->num_funcs && !rng; k++)
                if (k != i && ctx->funcs[k].confirmed
                    && fa > ctx->funcs[k].addr && fa < ctx->funcs[k].end_addr) rng = true;
            bool own = (off < ctx->rom_size && ctx->byte_owner[off]
                        && ctx->byte_owner[off] != fa);
            if (rng) by_range++;
            if (own) by_owner++;
        }
        fprintf(stderr, "  interior functions: %d by end_addr range, %d by exact ownership\n",
                by_range, by_owner);
    }

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

/* Drop functions whose entry byte the trace already proved is DATA.
 *
 * A jal to a skip-after-call helper is followed by inline argument bytes; the
 * trace marks them 'D' and steps over them. Nothing stops a function being
 * created there anyway, and then those bytes get DECODED. Galactic Pinball's
 * sub_07F5168C ends with `jal 0x07F417B8` whose 8 argument bytes at 0x07F51728
 * became a function -- and the first halfword of that data, 0x9800, decodes as
 * `bp .`, a branch to itself. The preceding function's fall-through jumps
 * straight into it and the game spins there forever: that is the whole of
 * Galactic Pinball's blank screen.
 *
 * Give the range back to the function that traced those bytes, so its own
 * fall-through lands past the arguments where execution really resumes. */
void v810_drop_data_funcs(v810_ctx_t *ctx) {
    if (!ctx->code_map || !ctx->byte_owner) return;
    int dropped = 0;
    for (int i = 0; i < ctx->num_funcs; i++) {
        uint32_t off = addr_to_offset(ctx, ctx->funcs[i].addr);
        if (off >= ctx->rom_size) continue;
        if (ctx->code_map[off] != 'D') continue;
        if (ctx->funcs[i].is_interrupt) continue;
        uint32_t own = ctx->byte_owner[off];
        if (!own || own == ctx->funcs[i].addr) continue;
        for (int k = 0; k < ctx->num_funcs; k++) {
            if (ctx->funcs[k].addr != own) continue;
            if (ctx->funcs[i].end_addr > ctx->funcs[k].end_addr)
                ctx->funcs[k].end_addr = ctx->funcs[i].end_addr;
            break;
        }
        ctx->funcs[i].confirmed = false;
        ctx->funcs[i].addr = 0;   /* skipped by the emitter */
        dropped++;
    }
    fprintf(stderr, "  dropped %d function(s) sitting on inline call data\n", dropped);
}

/* Detect skip-after-call helpers automatically.
 *
 * The V810 long-call idiom passes arguments INLINE, in the bytes right after
 * the `jal`. The callee reads them through r31 (which holds the return
 * address) and then returns past them:
 *
 *     ld.w  0[r31], r21
 *     ld.w  4[r31], r22
 *     ...
 *     add   8, r31
 *     jmp   [r31]
 *
 * Until now the only way the recompiler learned about those bytes was a
 * hand-written `skip <target> <n>` hint. Without it the argument bytes get
 * decoded as code, and whatever they happen to encode runs. Galactic Pinball's
 * arguments at 0x07F51728 start with 0x9800, which decodes as `bp .` -- a
 * branch to itself. The caller falls straight into it and the game spins there
 * for the rest of the run.
 *
 * The `add N, r31` immediately before `jmp [r31]` names the argument size
 * exactly, so detect it rather than asking someone to write it down. Runs
 * before analysis over the targets of a linear jal scan, because the trace
 * needs the skip amount while it is walking the CALLERS. */
void v810_detect_skip_funcs(v810_ctx_t *ctx) {
    int found = 0;
    for (uint32_t off = 0; off + 4 <= ctx->rom_size; off += 2) {
        v810_insn_t j;
        if (!v810_decode(ctx->rom, off, ctx->rom_size, &j)) continue;
        if (j.opcode != 0x2B) continue;                 /* jal */
        uint32_t target = v810_normalize_rom_addr(ctx,
                              ROM_OFF_TO_ADDR(off, ctx->rom_base) + j.imm);
        if (v810_get_skip_bytes(ctx, target)) continue; /* already known */
        uint32_t toff = addr_to_offset(ctx, target);
        if (toff >= ctx->rom_size) continue;

        /* Walk the callee to its first return, remembering the previous
         * instruction, and see whether that pair is `add N, r31` + `jmp [r31]`. */
        int32_t prev_add = -1;
        for (int n = 0; n < 64 && toff + 2 <= ctx->rom_size; n++) {
            v810_insn_t k;
            if (!v810_decode(ctx->rom, toff, ctx->rom_size, &k)) break;
            if (k.opcode == 0x06 && k.reg1 == 31) {     /* jmp [r31] */
                if (prev_add > 0 && prev_add <= 64 && (prev_add & 1) == 0) {
                    if (ctx->num_skip_funcs >= ctx->max_skip_funcs) {
                        ctx->max_skip_funcs *= 2;
                        ctx->skip_funcs = realloc(ctx->skip_funcs,
                            ctx->max_skip_funcs * sizeof(ctx->skip_funcs[0]));
                    }
                    ctx->skip_funcs[ctx->num_skip_funcs].target = target;
                    ctx->skip_funcs[ctx->num_skip_funcs].skip_bytes = (uint32_t)prev_add;
                    ctx->num_skip_funcs++;
                    found++;
                }
                break;
            }
            if (k.opcode == 0x19 || k.opcode == 0x2A) break;   /* reti / jr */
            prev_add = (k.opcode == 0x11 && k.reg2 == 31) ? k.imm : -1;
            toff += k.size;
        }
    }
    fprintf(stderr, "  skip-after-call helpers detected: %d\n", found);
}
