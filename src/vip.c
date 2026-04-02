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

/* VRAM access (addresses below 0x60000) */
static inline bool is_vram_addr(vb_addr_t addr) {
    return addr < 0x60000;
}

/* Register access (0x5F800 - 0x5FFFF) */
static inline bool is_reg_addr(vb_addr_t addr) {
    return addr >= VB_VIP_REG_BASE && addr < 0x60000;
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
    /* CHR data: 4 segments, 512 chars each
     * Segment 0-1: 0x06000 (chars 0-511)
     * Segment 2-3: 0x0E000 (chars 512-1023)
     */
    uint32_t chr_base;
    if (chr_index < 512) {
        chr_base = 0x06000 + chr_index * 16;
    } else {
        chr_base = 0x0E000 + (chr_index - 512) * 16;
    }
    /* Each row: 2 bytes, pixel data in 2-bit pairs */
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

    /* Copy tile pixel data from BGMap segment 3 (0x26000) to CHR 0-1 (0x06000)
     * if CHR is empty but BGMap segment 3 has tile data.
     * Mario's Tennis stores CHR data in the BGMap area. */
    if (vram[0x06000] == 0 && vram[0x06001] == 0 && vram[0x26000] != 0) {
        memcpy(&vram[0x06000], &vram[0x26000], 0x2000); /* 8KB = 512 tiles */
    }
    /* Also try segment at 0x24000 → CHR 2-3 (0x0E000) */
    if (vram[0x0E000] == 0 && vram[0x0E001] == 0 && vram[0x24000] != 0) {
        memcpy(&vram[0x0E000], &vram[0x24000], 0x2000);
    }

    /* Clear to background color
     * SDL_PIXELFORMAT_RGBA8888: R=bits31:24, G=23:16, B=15:8, A=7:0 */
    uint8_t bkcol = reg_bkcol & 0x03;
    uint8_t bg_intensity = bkcol * 85;
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
     *   +0 (HEAD): bits 15:14=BGM type, bit 13=right eye, bit 12=left eye,
     *              bits 11:10=SCX (size), bits 9:8=SCY, bit 6=OVER,
     *              bit 5:4=END (1=last world)
     *   +2 (GX):   signed 16-bit, screen X position
     *   +4 (GP):   signed 16-bit, parallax
     *   +6 (GY):   signed 16-bit, screen Y position
     *   +8 (MX):   signed 16-bit, map X scroll
     *   +10 (MP):  signed 16-bit, map parallax
     *   +12 (MY):  signed 16-bit, map Y scroll
     *   +14 (W):   width in pixels - 1
     *   +16 (H):   height in pixels - 1
     *   +28 (PARAM): BGMap base
     *   +30 (OVERPLN): overplane char
     */
    static int render_dbg = 0;
    render_dbg++;

    for (int w = 31; w >= 0; w--) {
        uint32_t wa = 0x3D800 + w * 32;

        uint16_t head = vram_read16(wa + 0);

        /* Check END bit — if set, stop rendering */
        if (head & 0x0040) {
            if (render_dbg <= 3) fprintf(stderr, "RENDER: World %d END (HEAD=%04X)\n", w, head);
            break;
        }

        /* Check if this world is active for the requested eye */
        int lon = (head >> 15) & 1;  /* Left eye on */
        /* For now render all active worlds regardless of eye */
        if (!lon && !(head & 0x4000)) continue; /* Neither eye enabled */

        int bgm_type = (head >> 12) & 0x03; /* Background type */

        int16_t gx = (int16_t)vram_read16(wa + 2);
        int16_t gy = (int16_t)vram_read16(wa + 6);
        int16_t mx = (int16_t)vram_read16(wa + 8);
        int16_t my = (int16_t)vram_read16(wa + 12);
        uint16_t w_width = vram_read16(wa + 14);
        uint16_t w_height = vram_read16(wa + 16);

        uint16_t param = vram_read16(wa + 28);
        int bgmap_base = (param & 0x0F) * 0x2000; /* BGMap segment × 8KB */

        /* Palette selection from GPLT registers */
        uint16_t pal0 = reg_gplt[0], pal1 = reg_gplt[1];
        uint16_t pal2 = reg_gplt[2], pal3 = reg_gplt[3];

        if (render_dbg <= 3) {
            fprintf(stderr, "RENDER: World %d HEAD=%04X type=%d GX=%d GY=%d W=%d H=%d PARAM=%04X bgmap=0x%05X\n",
                    w, head, bgm_type, gx, gy, w_width, w_height, param, 0x20000 + bgmap_base);
        }

        if (bgm_type <= 2) { /* Normal, H-bias, or Affine (treat all as normal for now) */
            /* Normal or H-bias background */
            int scx = 1 << (((head >> 10) & 0x03) + 6); /* 64, 128, 256, 512 cells */
            int scy = 1 << (((head >> 8) & 0x03) + 6);

            for (int sy = 0; sy <= (int)w_height && sy < VB_SCREEN_HEIGHT; sy++) {
                int screen_y = gy + sy;
                if (screen_y < 0 || screen_y >= VB_SCREEN_HEIGHT) continue;

                for (int sx = 0; sx <= (int)w_width && sx < VB_SCREEN_WIDTH; sx++) {
                    int screen_x = gx + sx;
                    if (screen_x < 0 || screen_x >= VB_SCREEN_WIDTH) continue;

                    /* Map coordinates */
                    int map_x = (mx + sx) & ((scx * 8) - 1);
                    int map_y = (my + sy) & ((scy * 8) - 1);

                    /* Which cell in the BGMap */
                    int cell_x = map_x / 8;
                    int cell_y = map_y / 8;

                    /* BGMap segment: 64 cells wide */
                    int seg_x = cell_x / 64;
                    int seg_y = cell_y / 64;
                    int local_x = cell_x % 64;
                    int local_y = cell_y % 64;

                    /* BGMap entry address */
                    int seg_cols = scx / 64;
                    int seg_idx = seg_y * seg_cols + seg_x;
                    uint32_t bgmap_addr = 0x20000 + (bgmap_base + seg_idx * 0x2000);
                    bgmap_addr &= (VB_VRAM_SIZE - 1);
                    uint32_t cell_addr = bgmap_addr + (local_y * 64 + local_x) * 2;
                    cell_addr &= (VB_VRAM_SIZE - 1);

                    uint16_t cell = vram_read16(cell_addr);
                    if (cell == 0) continue; /* Empty cell */

                    int chr_index = cell & 0x07FF;
                    int hflip = (cell >> 13) & 1;
                    int vflip = (cell >> 12) & 1;
                    int pal_idx = (cell >> 14) & 0x03;

                    /* Pixel within tile */
                    int tile_x = map_x % 8;
                    int tile_y = map_y % 8;
                    if (hflip) tile_x = 7 - tile_x;
                    if (vflip) tile_y = 7 - tile_y;

                    uint8_t pixel = chr_get_pixel(chr_index, tile_x, tile_y);
                    if (pixel == 0) continue; /* Transparent */

                    /* Apply palette */
                    uint16_t pal = (pal_idx == 0) ? pal0 :
                                   (pal_idx == 1) ? pal1 :
                                   (pal_idx == 2) ? pal2 : pal3;
                    uint8_t color = apply_palette(pixel, pal);
                    if (color == 0) continue;

                    /* Map to red intensity (RGBA8888: R high byte, A low byte) */
                    uint8_t intensity = color * 85;
                    out_rgba[screen_y * VB_SCREEN_WIDTH + screen_x] =
                        ((uint32_t)intensity << 24) | 0xFF;
                }
            }
        }
    }
}

void vb_vip_frame_advance(void) {
    frame_count++;

    /* Simulate frame timing: set GAMESTART and FRAMESTART interrupts */
    reg_intpnd |= VB_VIP_INT_GAMESTART;
    reg_intpnd |= VB_VIP_INT_FRAMESTART;
    reg_intpnd |= VB_VIP_INT_LFBEND;
    reg_intpnd |= VB_VIP_INT_RFBEND;
    reg_intpnd |= VB_VIP_INT_XPEND;

    /* Set display and drawing status bits that games poll for */
    reg_dpstts |= 0x0002;  /* Display on */
    reg_dpstts ^= 0x003C;  /* Toggle SCANRDY bits */

    /* Mark drawing complete so games don't poll XPSTTS forever */
    reg_xpstts |= 0x0042;  /* XPEN + bit 6 (drawing done) */
}
