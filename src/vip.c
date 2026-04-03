/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * VIP stub implementation. Accepts register/VRAM writes, returns
 * sensible status values so games don't hang waiting for frame signals.
 * Rendering is a TODO.
 */

#include "vbrecomp/vip.h"
#include <string.h>
#include <stdio.h>

/* VRAM backing store */
static uint8_t vram[VB_VRAM_SIZE];

uint8_t *vb_vip_get_vram(void) { return vram; }

/*
 * Map a VIP address to the actual VRAM offset, handling mirrors.
 *
 * VB VRAM layout (128KB framebuffer/CHR + 128KB BGMap/World/OAM):
 *   0x00000-0x05FFF: Left framebuffer 0
 *   0x06000-0x07FFF: CHR segments 0-1 (8KB, 512 chars)
 *   0x08000-0x0DFFF: Left framebuffer 1
 *   0x0E000-0x0FFFF: CHR segments 2-3 (8KB, 512 chars)
 *   0x10000-0x15FFF: Right framebuffer 0
 *   0x16000-0x17FFF: MIRROR of CHR 0-1 (same memory as 0x06000)
 *   0x18000-0x1DFFF: Right framebuffer 1
 *   0x1E000-0x1FFFF: MIRROR of CHR 2-3 (same memory as 0x0E000)
 *   0x20000-0x3D7FF: BGMap segments 0-13
 *   0x3D800-0x3DBFF: World attributes
 *   0x3E000-0x3FFFF: OAM
 *
 * Addresses >= 0x40000 mirror: FB/CHR portion via & 0x1FFFF,
 * BGMap portion stays in 0x20000-0x3FFFF range.
 */
static inline uint32_t vip_map_addr(uint32_t addr) {
    /* The VB VIP has two memory regions:
     * - FB/CHR bank: 128KB at 0x00000-0x1FFFF (physical)
     * - BGMap/World/OAM bank: 128KB at 0x20000-0x3FFFF (physical)
     *
     * For addresses >= 0x40000, determine which bank:
     * - If the address modulo 0x40000 falls in 0x20000-0x3FFFF → BGMap
     * - Otherwise → FB/CHR, masked to 0x1FFFF
     */
    if (addr >= 0x40000) {
        uint32_t in_block = addr & 0x3FFFF;
        if (in_block >= 0x20000) {
            addr = in_block; /* BGMap bank */
        } else {
            /* FB/CHR bank — wrap within 128KB */
            addr = addr & 0x1FFFF;
        }
    }

    /* CHR mirror within FB/CHR bank:
     * 0x16000-0x17FFF mirrors 0x06000-0x07FFF (same physical CHR RAM)
     * 0x1E000-0x1FFFF mirrors 0x0E000-0x0FFFF */
    if (addr >= 0x16000 && addr < 0x18000) {
        addr -= 0x10000;
    } else if (addr >= 0x1E000 && addr < 0x20000) {
        addr -= 0x10000;
    }

    return addr;
}

/* VIP registers */
static uint16_t reg_intpnd;
static uint16_t reg_intenb;
static uint16_t reg_dpstts;
static uint16_t reg_dpctrl;
static uint16_t reg_brta, reg_brtb, reg_brtc;
static uint16_t reg_rest;
static uint16_t reg_frmcyc;
static uint16_t reg_xpstts;
static uint16_t reg_xpctrl;
static uint16_t reg_spt[4];
static uint16_t reg_gplt[4];   /* BG palettes */
static uint16_t reg_jplt[4];   /* OBJ palettes */
static uint16_t reg_bkcol;

/* Frame counter for simulating VIP timing */
static uint32_t frame_count;

void vb_vip_init(void) {
    memset(vram, 0, VB_VRAM_SIZE);
    reg_intpnd = 0;
    reg_intenb = 0;
    reg_dpstts = 0;
    reg_dpctrl = 0;
    reg_brta = reg_brtb = reg_brtc = 0;
    reg_rest = 0;
    reg_frmcyc = 0;
    reg_xpstts = 0;
    reg_xpctrl = 0;
    memset(reg_spt, 0, sizeof(reg_spt));
    memset(reg_gplt, 0, sizeof(reg_gplt));
    memset(reg_jplt, 0, sizeof(reg_jplt));
    reg_bkcol = 0;
    frame_count = 0;
}

/* Register access (0x5F800 - 0x5FFFF) */
static inline bool is_reg_addr(vb_addr_t addr) {
    return addr >= VB_VIP_REG_BASE && addr < 0x60000;
}

/* VRAM access — VIP address space mirrors VRAM throughout */
static inline bool is_vram_addr(vb_addr_t addr) {
    return !is_reg_addr(addr);
}

static uint16_t reg_read(vb_addr_t offset) {
    switch (offset) {
    case VB_VIP_INTPND:  return reg_intpnd;
    case VB_VIP_INTENB:  return reg_intenb;
    case VB_VIP_INTCLR:  return 0; /* Write-only */
    case VB_VIP_DPSTTS:  return reg_dpstts | 0x003E; /* Always report display ready */
    case VB_VIP_DPCTRL:  return reg_dpctrl;
    case VB_VIP_BRTA:    return reg_brta;
    case VB_VIP_BRTB:    return reg_brtb;
    case VB_VIP_BRTC:    return reg_brtc;
    case VB_VIP_REST:    return reg_rest;
    case VB_VIP_FRMCYC:  return reg_frmcyc;
    case VB_VIP_XPSTTS:  return reg_xpstts | 0x007E; /* Always report drawing done + DPBSY/SCANRDY */
    case VB_VIP_XPCTRL:  return reg_xpctrl;
    case VB_VIP_SPT0:    return reg_spt[0];
    case VB_VIP_SPT1:    return reg_spt[1];
    case VB_VIP_SPT2:    return reg_spt[2];
    case VB_VIP_SPT3:    return reg_spt[3];
    case VB_VIP_GPLT0:   return reg_gplt[0];
    case VB_VIP_GPLT1:   return reg_gplt[1];
    case VB_VIP_GPLT2:   return reg_gplt[2];
    case VB_VIP_GPLT3:   return reg_gplt[3];
    case VB_VIP_JPLT0:   return reg_jplt[0];
    case VB_VIP_JPLT1:   return reg_jplt[1];
    case VB_VIP_JPLT2:   return reg_jplt[2];
    case VB_VIP_JPLT3:   return reg_jplt[3];
    case VB_VIP_BKCOL:   return reg_bkcol;
    default:             return 0;
    }
}

static void reg_write(vb_addr_t offset, uint16_t val) {
    switch (offset) {
    case VB_VIP_INTPND:  break; /* Read-only */
    case VB_VIP_INTENB:
        if (val != reg_intenb) {
            fprintf(stderr, "VIP: INTENB changed 0x%04X -> 0x%04X\n", reg_intenb, val);
        }
        reg_intenb = val;
        break;
    case VB_VIP_INTCLR:  reg_intpnd &= ~val; break;
    case VB_VIP_DPSTTS:  break; /* Read-only */
    case VB_VIP_DPCTRL:
        if (val != reg_dpctrl) {
            fprintf(stderr, "VIP: DPCTRL changed 0x%04X -> 0x%04X\n", reg_dpctrl, val);
        }
        reg_dpctrl = val;
        /* If display enabled, set DPSTTS to reflect it */
        if (val & 0x0002) {
            reg_dpstts |= 0x0002;  /* Display on */
        }
        break;
    case VB_VIP_BRTA:    reg_brta = val & 0xFF; break;
    case VB_VIP_BRTB:    reg_brtb = val & 0xFF; break;
    case VB_VIP_BRTC:    reg_brtc = val & 0xFF; break;
    case VB_VIP_REST:    reg_rest = val & 0xFF; break;
    case VB_VIP_FRMCYC:  reg_frmcyc = val & 0x0F; break;
    case VB_VIP_XPSTTS:  break; /* Read-only */
    case VB_VIP_XPCTRL:
        reg_xpctrl = val;
        /* If drawing requested, immediately mark as done (stub) */
        if (val & 0x0002) {
            reg_xpstts |= 0x0002;  /* Drawing started */
            reg_xpstts |= 0x0040;  /* Drawing complete / SBOUT ready */
            reg_xpstts &= ~0x000C; /* Clear "drawing in progress" */
            reg_intpnd |= VB_VIP_INT_XPEND; /* Drawing finished */
        }
        break;
    case VB_VIP_SPT0:    reg_spt[0] = val & 0x03FF; break;
    case VB_VIP_SPT1:    reg_spt[1] = val & 0x03FF; break;
    case VB_VIP_SPT2:    reg_spt[2] = val & 0x03FF; break;
    case VB_VIP_SPT3:    reg_spt[3] = val & 0x03FF; break;
    case VB_VIP_GPLT0:   reg_gplt[0] = val & 0xFC; break;
    case VB_VIP_GPLT1:   reg_gplt[1] = val & 0xFC; break;
    case VB_VIP_GPLT2:   reg_gplt[2] = val & 0xFC; break;
    case VB_VIP_GPLT3:   reg_gplt[3] = val & 0xFC; break;
    case VB_VIP_JPLT0:   reg_jplt[0] = val & 0xFC; break;
    case VB_VIP_JPLT1:   reg_jplt[1] = val & 0xFC; break;
    case VB_VIP_JPLT2:   reg_jplt[2] = val & 0xFC; break;
    case VB_VIP_JPLT3:   reg_jplt[3] = val & 0xFC; break;
    case VB_VIP_BKCOL:   reg_bkcol = val & 0x03; break;
    default: break;
    }
}

/* --- Public interface --- */

uint8_t vb_vip_read8(vb_addr_t addr) {
    if (is_reg_addr(addr)) {
        uint16_t val = reg_read((addr - VB_VIP_REG_BASE) & ~1);
        return (addr & 1) ? (uint8_t)(val >> 8) : (uint8_t)(val);
    }
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        return (off < VB_VRAM_SIZE) ? vram[off] : 0;
    }
    return 0;
}

uint16_t vb_vip_read16(vb_addr_t addr) {
    if (is_reg_addr(addr)) {
        return reg_read((addr - VB_VIP_REG_BASE) & ~1);
    }
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        if (off + 1 < VB_VRAM_SIZE)
            return (uint16_t)vram[off] | ((uint16_t)vram[off + 1] << 8);
    }
    return 0;
}

uint32_t vb_vip_read32(vb_addr_t addr) {
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        if (off + 3 < VB_VRAM_SIZE)
            return (uint32_t)vram[off]
                 | ((uint32_t)vram[off + 1] << 8)
                 | ((uint32_t)vram[off + 2] << 16)
                 | ((uint32_t)vram[off + 3] << 24);
    }
    /* 32-bit register reads: just compose from two 16-bit reads */
    return (uint32_t)vb_vip_read16(addr) | ((uint32_t)vb_vip_read16(addr + 2) << 16);
}

static int world_write_count = 0;
void vb_vip_write8(vb_addr_t addr, uint8_t val) {
    if (is_reg_addr(addr)) {
        /* Byte write to register: read-modify-write the 16-bit register */
        uint16_t offset = (addr - VB_VIP_REG_BASE) & ~1;
        uint16_t cur = reg_read(offset);
        if (addr & 1) {
            cur = (cur & 0x00FF) | ((uint16_t)val << 8);
        } else {
            cur = (cur & 0xFF00) | val;
        }
        reg_write(offset, cur);
        return;
    }
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        if (off < VB_VRAM_SIZE) vram[off] = val;
    }
}

void vb_vip_write16(vb_addr_t addr, uint16_t val) {
    /* Track writes to world attributes */
    {
        uint32_t mapped = vip_map_addr(addr);
        if (mapped >= 0x3D800 && mapped < 0x3DC00 && val != 0) {
            world_write_count++;
            if (world_write_count <= 5) {
                int world = (mapped - 0x3D800) / 32;
                int offset = (mapped - 0x3D800) % 32;
                fprintf(stderr, "WORLD WRITE16: world=%d off=%d val=%04X\n", world, offset, val);
            }
        }
    }
    if (is_reg_addr(addr)) {
        reg_write((addr - VB_VIP_REG_BASE) & ~1, val);
        return;
    }
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        if (off + 1 < VB_VRAM_SIZE) {
            vram[off]     = (uint8_t)(val);
            vram[off + 1] = (uint8_t)(val >> 8);
        }
    }
}

void vb_vip_write32(vb_addr_t addr, uint32_t val) {
    if (is_vram_addr(addr)) {
        uint32_t off = vip_map_addr(addr);
        if (off + 3 < VB_VRAM_SIZE) {
            vram[off]     = (uint8_t)(val);
            vram[off + 1] = (uint8_t)(val >> 8);
            vram[off + 2] = (uint8_t)(val >> 16);
            vram[off + 3] = (uint8_t)(val >> 24);
        }
        return;
    }
    vb_vip_write16(addr, (uint16_t)(val));
    vb_vip_write16(addr + 2, (uint16_t)(val >> 16));
}

/*
 * Read a 16-bit value from VRAM (little-endian)
 */
static inline uint16_t vram_read16(uint32_t addr) {
    addr &= (VB_VRAM_SIZE - 1);
    return (uint16_t)vram[addr] | ((uint16_t)vram[addr + 1] << 8);
}

/*
 * Get a 2-bit pixel from a character (8x8 tile).
 * Characters are stored as 8 rows of 2 bytes each (16 bytes per char).
 * Each row has 8 pixels × 2 bits = 16 bits.
 */
static inline uint8_t chr_get_pixel(int chr_index, int px, int py) {
    /* VB has 1024 unique characters; indices 1024-2047 mirror 0-1023 */
    chr_index &= 0x3FF;
    uint32_t chr_base;
    if (chr_index < 512) {
        chr_base = 0x06000 + chr_index * 16;
    } else {
        chr_base = 0x0E000 + (chr_index - 512) * 16;
    }
    /* If standard CHR is empty, also check the staging area at 0x38000 */
    if (vram[chr_base] == 0 && vram[chr_base + 2] == 0 &&
        vram[chr_base + 4] == 0 && vram[chr_base + 6] == 0) {
        uint32_t alt = 0x38000 + chr_index * 16;
        if (alt + 15 < VB_VRAM_SIZE) chr_base = alt;
    }
    uint16_t row_data = vram_read16(chr_base + py * 2);
    return (row_data >> (px * 2)) & 0x03;
}

/*
 * Apply palette: map 2-bit pixel through palette register
 * Palette format: bits [7:6]=color3, [5:4]=color2, [3:2]=color1, [1:0]=color0
 * Color 0 is always transparent (0)
 */
static inline uint8_t apply_palette(uint8_t pixel, uint16_t palette) {
    if (pixel == 0) return 0; /* Transparent */
    return (palette >> (pixel * 2)) & 0x03;
}

void vb_vip_render(uint32_t *out_rgba, int eye) {
    (void)eye;

    /* Sync CHR from game's staging area (0x38000/0x3A000) every frame.
     * The game dynamically updates tile data here and the VIP interrupt
     * handler should copy it to CHR, but that handler doesn't fully run. */
    if (vram[0x38000] != 0 || vram[0x38010] != 0) {
        memcpy(&vram[0x06000], &vram[0x38000], 0x2000);
    }
    if (vram[0x3A000] != 0 || vram[0x3A010] != 0) {
        memcpy(&vram[0x0E000], &vram[0x3A000], 0x2000);
    }

    /* Clear to background color
     * SDL_PIXELFORMAT_RGBA8888: R=bits31:24, G=23:16, B=15:8, A=7:0 */
    uint8_t bkcol = reg_bkcol & 0x03;
    uint8_t bg_intensity = bkcol ? (64 + bkcol * 64) : 0;
    uint32_t bg_color = ((uint32_t)bg_intensity << 24) | 0xFF; /* R=intensity, A=FF */
    for (int i = 0; i < VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT; i++) {
        out_rgba[i] = bg_color;
    }

    /*
     * Render worlds (display layers).
     * World attributes are at VRAM 0x3D800, 32 bytes per world, 32 worlds.
     * Worlds are rendered from 31 down to 0 (31 = backmost).
     *
     * World attribute format (16 bytes used):
     *   +0 (HEAD): bit 15=LON, bit 14=RON, bits 13:12=BGM type,
     *              bits 11:10=SCX (size), bits 9:8=SCY, bit 7=OVER,
     *              bit 6=END (1=last world)
     *   +2 (GX):   signed 16-bit, screen X position
     *   +4 (GP):   signed 16-bit, parallax
     *   +6 (GY):   signed 16-bit, screen Y position
     *   +8 (MX):   signed 16-bit, map X scroll
     *   +10 (MP):  signed 16-bit, map parallax
     *   +12 (MY):  signed 16-bit, map Y scroll
     *   +14 (W):   width in pixels - 1
     *   +16 (H):   height in pixels - 1
     *   +18 (PARAM): BGMap base / param table pointer
     *   +20 (OVERPLN): overplane char
     */
    static int render_dbg = 0;
    static int tiles_drawn = 0;
    render_dbg++;
    tiles_drawn = 0;

    int obj_group = 3; /* Next OBJ group to render (counts down 3→0) */

    for (int w = 31; w >= 0; w--) {
        uint32_t wa = 0x3D800 + w * 32;

        uint16_t head = vram_read16(wa + 0);

        /* Check END bit — if set, stop rendering */
        if (head & 0x0040) {
            if (render_dbg == 300) fprintf(stderr, "RENDER: World %d END (HEAD=%04X)\n", w, head);
            break;
        }

        /* Check if this world is active for the requested eye */
        int lon = (head >> 15) & 1;  /* Left eye on */
        int ron = (head >> 14) & 1;  /* Right eye on */
        if (eye == 0 && !lon) continue; /* Left eye render, world not enabled for left */
        if (eye == 1 && !ron) continue; /* Right eye render, world not enabled for right */
        if (!lon && !ron) continue;     /* Neither eye enabled */

        int bgm_type = (head >> 12) & 0x03; /* Background type */

        int16_t gx = (int16_t)vram_read16(wa + 2);
        int16_t gy = (int16_t)vram_read16(wa + 6);
        int16_t mx = (int16_t)vram_read16(wa + 8);
        int16_t my = (int16_t)vram_read16(wa + 12);
        uint16_t w_width = vram_read16(wa + 14);
        uint16_t w_height = vram_read16(wa + 16);

        uint16_t param = vram_read16(wa + 18); /* PARAM at offset 0x12 */
        int bgmap_base = (param & 0x0F) * 0x2000; /* BGMap segment × 8KB */

        /* Palette selection from GPLT registers */
        uint16_t pal0 = reg_gplt[0], pal1 = reg_gplt[1];
        uint16_t pal2 = reg_gplt[2], pal3 = reg_gplt[3];

        if (render_dbg == 300) {
        /* Check where tile data actually lives */
        fprintf(stderr, "Checking VRAM for tile patterns:\n");
        uint32_t check_addrs[] = {0x06000, 0x0E000, 0x24000, 0x26000, 0x38000, 0x39000, 0x3A000};
        for (int a = 0; a < 7; a++) {
            uint32_t base = check_addrs[a];
            int nz = 0;
            for (int i = 0; i < 256; i++) { if (vram[base+i]) nz++; }
            fprintf(stderr, "  @0x%05X: %3d/256 nonzero | first 8hw: ", base, nz);
            for (int i = 0; i < 8; i++) fprintf(stderr, "%04X ", vram_read16(base + i*2));
            fprintf(stderr, "\n");
        }
    }
    if (render_dbg == 300 || render_dbg == 500 || render_dbg == 800) {
            fprintf(stderr, "RENDER F%d: World %d HEAD=%04X type=%d GX=%d GY=%d MX=%d MY=%d W=%d H=%d PARAM=%04X bgmap=0x%05X\n",
                    render_dbg, w, head, bgm_type, gx, gy, mx, my, w_width, w_height, param, 0x20000 + bgmap_base);
            if (bgm_type == 2) {
                uint32_t pt = 0x20000 + ((param & 0xFFF0) * 2);
                for (int i = 0; i < 3 && i <= (int)w_height; i++) {
                    uint32_t pe = (pt + i * 16) & (VB_VRAM_SIZE - 1);
                    fprintf(stderr, "  scanline %d: MX=%d MP=%d MY=%d DX=%d DY=%d\n", i,
                            (int16_t)vram_read16(pe), (int16_t)vram_read16(pe+2),
                            (int16_t)vram_read16(pe+4), (int16_t)vram_read16(pe+6),
                            (int16_t)vram_read16(pe+8));
                }
                /* Dump a few BGMap cells from this world's source region */
                int16_t smx = (int16_t)vram_read16(pt);
                int16_t smy = (int16_t)vram_read16(pt + 4);
                int base_cx = (smx >> 3) / 8;  /* cell X from 13.3 MX */
                int base_cy = (smy >> 3) / 8;  /* cell Y from 13.3 MY */
                int bbase = (param & 0x0F) * 0x2000;
                fprintf(stderr, "  BGMap cells at row %d: ", base_cy);
                for (int c = 0; c < 8; c++) {
                    uint32_t ca = 0x20000 + bbase + ((base_cy * 64 + base_cx + c) * 2);
                    ca &= (VB_VRAM_SIZE - 1);
                    fprintf(stderr, "%04X ", vram_read16(ca));
                }
                fprintf(stderr, "\n");
            }
        }

        if (bgm_type == 3) {
            /* OBJ world — render sprites from the next OBJ group */
            if (obj_group < 0) continue;

            int obj_start, obj_end;
            switch (obj_group) {
            case 3: obj_start = reg_spt[2] + 1; obj_end = reg_spt[3]; break;
            case 2: obj_start = reg_spt[1] + 1; obj_end = reg_spt[2]; break;
            case 1: obj_start = reg_spt[0] + 1; obj_end = reg_spt[1]; break;
            default: obj_start = 0; obj_end = reg_spt[0]; break;
            }
            obj_group--;

            if (render_dbg == 300) {
                fprintf(stderr, "RENDER: OBJ group %d, OBJs %d-%d\n",
                        obj_group + 1, obj_start, obj_end);
            }

            for (int o = obj_end; o >= obj_start; o--) {
                uint32_t oa = 0x3E000 + o * 8;

                /* Halfword 0: JX[9:0] — signed 10-bit X */
                int16_t jx = (int16_t)(vram_read16(oa + 0) & 0x03FF);
                if (jx & 0x0200) jx |= (int16_t)0xFC00; /* Sign extend */

                /* Halfword 1: [15]=JLON [14]=JRON [13:10]=JP [7:0]=JY */
                uint16_t hw1 = vram_read16(oa + 2);
                int jlon = (hw1 >> 15) & 1;
                int jron = (hw1 >> 14) & 1;
                if (!jlon && !jron) continue;
                int16_t jy = (int16_t)(hw1 & 0xFF);

                /* Halfword 2: [15]=JHFLP [14]=JVFLP [10:0]=JCA */
                uint16_t hw2 = vram_read16(oa + 4);
                int jhflp = (hw2 >> 15) & 1;
                int jvflp = (hw2 >> 14) & 1;
                int jca = hw2 & 0x07FF;

                /* Halfword 3: [15:14]=JPAL */
                uint16_t hw3 = vram_read16(oa + 6);
                int jpal = (hw3 >> 14) & 0x03;

                uint16_t pal = reg_jplt[jpal];

                /* Draw 8x8 tile */
                for (int py = 0; py < 8; py++) {
                    int sy = jy + py;
                    if (sy < 0 || sy >= VB_SCREEN_HEIGHT) continue;
                    for (int px = 0; px < 8; px++) {
                        int sx = jx + px;
                        if (sx < 0 || sx >= VB_SCREEN_WIDTH) continue;

                        int tx = jhflp ? (7 - px) : px;
                        int ty = jvflp ? (7 - py) : py;

                        uint8_t pixel = chr_get_pixel(jca, tx, ty);
                        if (pixel == 0) continue;

                        uint8_t color = apply_palette(pixel, pal);
                        if (color == 0) continue;

                        uint8_t intensity = 64 + color * 64;
                        out_rgba[sy * VB_SCREEN_WIDTH + sx] =
                            ((uint32_t)intensity << 24) | 0xFF;
                        tiles_drawn++;
                    }
                }
            }
        } else if (bgm_type == 2) {
            /* Affine mode — per-scanline parameter table */
            int scx = 1 << (((head >> 10) & 0x03) + 6);
            int scy = 1 << (((head >> 8) & 0x03) + 6);
            int over = (head >> 7) & 1;

            /* Parameter table: 8 halfwords per scanline (MX, MP, MY, DX, DY, pad, pad, pad) */
            uint32_t param_table = 0x20000 + ((param & 0xFFF0) * 2);

            for (int sy = 0; sy <= (int)w_height && sy < VB_SCREEN_HEIGHT; sy++) {
                int screen_y = gy + sy;
                if (screen_y < 0 || screen_y >= VB_SCREEN_HEIGHT) continue;

                /* Read per-scanline affine parameters */
                uint32_t pe = param_table + sy * 16; /* 8 halfwords = 16 bytes */
                pe &= (VB_VRAM_SIZE - 1);
                int16_t src_mx = (int16_t)vram_read16(pe + 0); /* 13.3 fixed */
                /* pe+2 = MP (parallax), skip for now */
                int16_t src_my = (int16_t)vram_read16(pe + 4); /* 13.3 fixed */
                int16_t src_dx = (int16_t)vram_read16(pe + 6); /* 7.9 fixed */
                int16_t src_dy = (int16_t)vram_read16(pe + 8); /* 7.9 fixed */

                /* Convert MX/MY from 13.3 to 7.9 fixed-point to match DX/DY */
                int32_t fx = (int32_t)src_mx << 6;
                int32_t fy = (int32_t)src_my << 6;

                for (int sx = 0; sx <= (int)w_width && sx < VB_SCREEN_WIDTH; sx++) {
                    int screen_x = gx + sx;
                    if (screen_x < 0 || screen_x >= VB_SCREEN_WIDTH) { fx += src_dx; continue; }

                    /* Source coordinates from fixed-point (>>9 for 7.9) */
                    int map_x = fx >> 9;
                    int map_y = fy >> 9;

                    /* Bounds check / wrapping */
                    int map_w = scx * 8;
                    int map_h = scy * 8;
                    if (over) {
                        /* OVER: out-of-range pixels are skipped (or use overplane) */
                        if (map_x < 0 || map_x >= map_w || map_y < 0 || map_y >= map_h) {
                            fx += src_dx;
                            continue;
                        }
                    } else {
                        map_x = map_x & (map_w - 1);
                        map_y = map_y & (map_h - 1);
                    }

                    /* Tile lookup — same as Normal mode */
                    int cell_x = map_x / 8;
                    int cell_y = map_y / 8;
                    int seg_x = cell_x / 64;
                    int seg_y = cell_y / 64;
                    int local_x = cell_x % 64;
                    int local_y = cell_y % 64;
                    int seg_cols = scx / 64;
                    int seg_idx = seg_y * seg_cols + seg_x;
                    uint32_t bgmap_addr = 0x20000 + (bgmap_base + seg_idx * 0x2000);
                    bgmap_addr &= (VB_VRAM_SIZE - 1);
                    uint32_t cell_addr = bgmap_addr + (local_y * 64 + local_x) * 2;
                    cell_addr &= (VB_VRAM_SIZE - 1);

                    uint16_t cell = vram_read16(cell_addr);
                    if (cell != 0) {
                        int chr_index = cell & 0x07FF;
                        int hflip = (cell >> 13) & 1;
                        int vflip = (cell >> 12) & 1;
                        int pal_idx = (cell >> 14) & 0x03;
                        int tile_x = map_x % 8;
                        int tile_y = map_y % 8;
                        if (hflip) tile_x = 7 - tile_x;
                        if (vflip) tile_y = 7 - tile_y;

                        uint8_t pixel = chr_get_pixel(chr_index, tile_x, tile_y);
                        if (pixel != 0) {
                            uint16_t pal = (pal_idx == 0) ? pal0 :
                                           (pal_idx == 1) ? pal1 :
                                           (pal_idx == 2) ? pal2 : pal3;
                            uint8_t color = apply_palette(pixel, pal);
                            if (color != 0) {
                                uint8_t intensity = 64 + color * 64;
                                out_rgba[screen_y * VB_SCREEN_WIDTH + screen_x] =
                                    ((uint32_t)intensity << 24) | 0xFF;
                                tiles_drawn++;
                            }
                        }
                    }
                    fx += src_dx;
                    fy += src_dy;
                }
            }
        } else {
            /* Normal (BGM=0) or H-bias (BGM=1, treated as normal for now) */
            int scx = 1 << (((head >> 10) & 0x03) + 6);
            int scy = 1 << (((head >> 8) & 0x03) + 6);

            for (int sy = 0; sy <= (int)w_height && sy < VB_SCREEN_HEIGHT; sy++) {
                int screen_y = gy + sy;
                if (screen_y < 0 || screen_y >= VB_SCREEN_HEIGHT) continue;

                for (int sx = 0; sx <= (int)w_width && sx < VB_SCREEN_WIDTH; sx++) {
                    int screen_x = gx + sx;
                    if (screen_x < 0 || screen_x >= VB_SCREEN_WIDTH) continue;

                    int map_x = (mx + sx) & ((scx * 8) - 1);
                    int map_y = (my + sy) & ((scy * 8) - 1);

                    int cell_x = map_x / 8;
                    int cell_y = map_y / 8;
                    int seg_x = cell_x / 64;
                    int seg_y = cell_y / 64;
                    int local_x = cell_x % 64;
                    int local_y = cell_y % 64;
                    int seg_cols = scx / 64;
                    int seg_idx = seg_y * seg_cols + seg_x;
                    uint32_t bgmap_addr = 0x20000 + (bgmap_base + seg_idx * 0x2000);
                    bgmap_addr &= (VB_VRAM_SIZE - 1);
                    uint32_t cell_addr = bgmap_addr + (local_y * 64 + local_x) * 2;
                    cell_addr &= (VB_VRAM_SIZE - 1);

                    uint16_t cell = vram_read16(cell_addr);
                    if (cell == 0) continue;

                    int chr_index = cell & 0x07FF;
                    int hflip = (cell >> 13) & 1;
                    int vflip = (cell >> 12) & 1;
                    int pal_idx = (cell >> 14) & 0x03;
                    int tile_x = map_x % 8;
                    int tile_y = map_y % 8;
                    if (hflip) tile_x = 7 - tile_x;
                    if (vflip) tile_y = 7 - tile_y;

                    uint8_t pixel = chr_get_pixel(chr_index, tile_x, tile_y);
                    if (pixel == 0) continue;

                    uint16_t pal = (pal_idx == 0) ? pal0 :
                                   (pal_idx == 1) ? pal1 :
                                   (pal_idx == 2) ? pal2 : pal3;
                    uint8_t color = apply_palette(pixel, pal);
                    if (color == 0) continue;

                    uint8_t intensity = 64 + color * 64;
                    out_rgba[screen_y * VB_SCREEN_WIDTH + screen_x] =
                        ((uint32_t)intensity << 24) | 0xFF;
                    tiles_drawn++;
                }
            }
        }
    }

    if (render_dbg == 300) {
        fprintf(stderr, "RENDER F%d: %d pixels drawn, CHR[0x06000]=%02X%02X\n",
                render_dbg, tiles_drawn, vram[0x06000], vram[0x06001]);
    }
}

void vb_vip_frame_advance(void) {
    frame_count++;

    /* Simulate frame timing: set all frame-related interrupt bits.
     * Games check INTPND for specific combinations (e.g. 0x303C)
     * before proceeding with OAM copy and display setup. */
    reg_intpnd |= VB_VIP_INT_LFBEND;      /* bit 1 */
    reg_intpnd |= VB_VIP_INT_RFBEND;      /* bit 2 */
    reg_intpnd |= VB_VIP_INT_GAMESTART;   /* bit 3 */
    reg_intpnd |= VB_VIP_INT_FRAMESTART;  /* bit 4 */
    reg_intpnd |= 0x0020;                 /* bit 5: GAMEEND */
    reg_intpnd |= 0x1000;                 /* bit 12 */
    reg_intpnd |= 0x2000;                 /* bit 13: SBHIT */
    reg_intpnd |= VB_VIP_INT_XPEND;       /* bit 14 */

    /* Set display and drawing status bits that games poll for */
    reg_dpstts |= 0x0002;  /* Display on */
    reg_dpstts ^= 0x003C;  /* Toggle SCANRDY bits */

    /* Mark drawing complete so games don't poll XPSTTS forever */
    reg_xpstts |= 0x0042;  /* XPEN + bit 6 (drawing done) */
}
