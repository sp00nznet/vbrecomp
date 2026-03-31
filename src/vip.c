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

/* VRAM backing store */
static uint8_t vram[VB_VRAM_SIZE];

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
    case VB_VIP_XPSTTS:  return reg_xpstts | 0x0040; /* Always report drawing done */
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
    case VB_VIP_INTENB:  reg_intenb = val; break;
    case VB_VIP_INTCLR:  reg_intpnd &= ~val; break;
    case VB_VIP_DPSTTS:  break; /* Read-only */
    case VB_VIP_DPCTRL:
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
        return vram[addr & (VB_VRAM_SIZE - 1)];
    }
    return 0;
}

uint16_t vb_vip_read16(vb_addr_t addr) {
    if (is_reg_addr(addr)) {
        static uint32_t last_vip_addr = 0;
        static int vip_repeat = 0;
        if (addr == last_vip_addr) {
            vip_repeat++;
            if (vip_repeat == 100000) {
                uint16_t val = reg_read((addr - VB_VIP_REG_BASE) & ~1);
                fprintf(stderr, "VIP POLL: reg 0x%04X = 0x%04X (100K reads)\n",
                        (unsigned)(addr - VB_VIP_REG_BASE), val);
            }
        } else {
            vip_repeat = 0;
            last_vip_addr = addr;
        }
        return reg_read((addr - VB_VIP_REG_BASE) & ~1);
    }
    if (is_vram_addr(addr)) {
        uint32_t off = addr & (VB_VRAM_SIZE - 1);
        return (uint16_t)vram[off] | ((uint16_t)vram[off + 1] << 8);
    }
    return 0;
}

uint32_t vb_vip_read32(vb_addr_t addr) {
    if (is_vram_addr(addr)) {
        uint32_t off = addr & (VB_VRAM_SIZE - 1);
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
        vram[addr & (VB_VRAM_SIZE - 1)] = val;
    }
}

void vb_vip_write16(vb_addr_t addr, uint16_t val) {
    if (is_reg_addr(addr)) {
        reg_write((addr - VB_VIP_REG_BASE) & ~1, val);
        return;
    }
    if (is_vram_addr(addr)) {
        uint32_t off = addr & (VB_VRAM_SIZE - 1);
        vram[off]     = (uint8_t)(val);
        vram[off + 1] = (uint8_t)(val >> 8);
    }
}

void vb_vip_write32(vb_addr_t addr, uint32_t val) {
    if (is_vram_addr(addr)) {
        uint32_t off = addr & (VB_VRAM_SIZE - 1);
        vram[off]     = (uint8_t)(val);
        vram[off + 1] = (uint8_t)(val >> 8);
        vram[off + 2] = (uint8_t)(val >> 16);
        vram[off + 3] = (uint8_t)(val >> 24);
        return;
    }
    vb_vip_write16(addr, (uint16_t)(val));
    vb_vip_write16(addr + 2, (uint16_t)(val >> 16));
}

void vb_vip_render(uint32_t *out_rgba, int eye) {
    (void)eye;

    /* TODO: Proper world/layer/OBJ rendering */
    /* For now, just dump the raw framebuffer contents as grayscale-ish red */

    /* Select which framebuffer based on eye and current display buffer */
    /* Left FB0: 0x00000, Left FB1: 0x08000 */
    /* Right FB0: 0x10000, Right FB1: 0x18000 */
    uint32_t fb_base = eye ? 0x10000 : 0x00000;
    /* TODO: double-buffer selection based on VIP state */

    /*
     * VB framebuffer format: 384x224, 2 bits per pixel
     * Stored column-major: each column is 224 pixels = 56 bytes (224*2/8)
     * 384 columns total = 384 * 56 = 21504 bytes per framebuffer
     */
    for (int col = 0; col < VB_SCREEN_WIDTH; col++) {
        uint32_t col_base = fb_base + col * 56; /* Not exact, simplified */
        for (int row = 0; row < VB_SCREEN_HEIGHT; row++) {
            int bit_offset = row * 2;
            int byte_idx = bit_offset / 8;
            int bit_pos = bit_offset % 8;
            uint8_t byte = vram[col_base + byte_idx];
            uint8_t pixel = (byte >> bit_pos) & 0x03;

            /* Map 2-bit pixel to RGBA red channel */
            uint8_t intensity = pixel * 85; /* 0, 85, 170, 255 */
            out_rgba[row * VB_SCREEN_WIDTH + col] =
                (0xFF << 24) | (intensity << 0); /* ABGR: alpha=FF, R=intensity */
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
