/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 */

#include "vbrecomp/mem.h"
#include "vbrecomp/vip.h"
#include "vbrecomp/vsu.h"
#include "vbrecomp/timer.h"
#include "vbrecomp/gamepad.h"
#include <string.h>
#include <stdio.h>

uint8_t vb_wram[VB_WRAM_SIZE];
uint8_t vb_sram[VB_SRAM_SIZE];

static const uint8_t *rom_ptr;
static uint32_t rom_sz;
static uint32_t rom_mask;

/* Hardware control registers at 0x02000000 */
#define HW_REG_CCR     0x00  /* Communication control */
#define HW_REG_CCSR    0x04  /* Comm. control shadow */
#define HW_REG_CDTR    0x08  /* Comm. data */
#define HW_REG_CDRR    0x0C  /* Comm. data receive */
#define HW_REG_TCR     0x10  /* Timer control */
#define HW_REG_TLR     0x14  /* Timer counter low */
#define HW_REG_THR     0x18  /* Timer counter high */
#define HW_REG_WCR     0x20  /* Wait control */
#define HW_REG_SCR     0x28  /* Serial (gamepad) control */
#define HW_REG_SDR     0x2C  /* Serial data low */
#define HW_REG_SDHR    0x30  /* Serial data high */

/* Round rom_size up to the nearest power of two for masking */
static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

void vb_mem_init(const uint8_t *rom_data, uint32_t rom_size) {
    memset(vb_wram, 0, VB_WRAM_SIZE);
    memset(vb_sram, 0, VB_SRAM_SIZE);
    rom_ptr = rom_data;
    rom_sz = rom_size;
    rom_mask = next_pow2(rom_size) - 1;
}

void vb_mem_shutdown(void) {
    rom_ptr = NULL;
    rom_sz = 0;
}

/* --- Hardware control register access --- */

static uint8_t hw_wcr = 0;  /* Wait control register */

static uint8_t hw_read8(vb_addr_t offset) {
    offset &= 0x3F;  /* Registers repeat within small range */

    switch (offset) {
    case HW_REG_TCR:
    case HW_REG_TLR:
    case HW_REG_THR:
        return vb_timer_read8(offset);

    case HW_REG_SCR:
    case HW_REG_SDR:
    case HW_REG_SDHR:
        return vb_gamepad_read8(offset);

    case HW_REG_WCR:
        return hw_wcr;

    default:
        return 0;
    }
}

static void hw_write8(vb_addr_t offset, uint8_t val) {
    offset &= 0x3F;

    switch (offset) {
    case HW_REG_TCR:
    case HW_REG_TLR:
    case HW_REG_THR:
        vb_timer_write8(offset, val);
        break;

    case HW_REG_SCR:
    case HW_REG_SDR:
    case HW_REG_SDHR:
        vb_gamepad_write8(offset, val);
        break;

    case HW_REG_WCR:
        hw_wcr = val;
        break;

    default:
        break;
    }
}

/* --- Bus dispatch --- */

/* Region is top nibble (bits 27:24 after masking to 28-bit space) */
#define REGION(addr) (((addr) >> 24) & 0x07)

uint8_t vb_mem_read8(vb_addr_t addr) {
    switch (REGION(addr)) {
    case 0: return vb_vip_read8(addr & 0x00FFFFFF);
    case 1: return vb_vsu_read8(addr & 0x00FFFFFF);
    case 2: return hw_read8(addr & 0x00FFFFFF);
    case 5: return vb_wram[addr & VB_WRAM_MASK];
    case 6: return vb_sram[addr & VB_SRAM_MASK];
    case 7: return rom_ptr ? rom_ptr[addr & rom_mask] : 0xFF;
    default: return 0;
    }
}

uint16_t vb_mem_read16(vb_addr_t addr) {
    switch (REGION(addr)) {
    case 0: return vb_vip_read16(addr & 0x00FFFFFF);
    case 5: {
        uint32_t off = addr & VB_WRAM_MASK;
        return (uint16_t)vb_wram[off] | ((uint16_t)vb_wram[off + 1] << 8);
    }
    case 6: {
        uint32_t off = addr & VB_SRAM_MASK;
        return (uint16_t)vb_sram[off] | ((uint16_t)vb_sram[off + 1] << 8);
    }
    case 7: {
        if (!rom_ptr) return 0xFFFF;
        uint32_t off = addr & rom_mask;
        return (uint16_t)rom_ptr[off] | ((uint16_t)rom_ptr[off + 1] << 8);
    }
    default:
        /* For regions without native 16-bit access, compose from bytes */
        return (uint16_t)vb_mem_read8(addr) | ((uint16_t)vb_mem_read8(addr + 1) << 8);
    }
}

uint32_t vb_mem_read32(vb_addr_t addr) {
    switch (REGION(addr)) {
    case 0: return vb_vip_read32(addr & 0x00FFFFFF);
    case 5: {
        uint32_t off = addr & VB_WRAM_MASK;
        return (uint32_t)vb_wram[off]
             | ((uint32_t)vb_wram[off + 1] << 8)
             | ((uint32_t)vb_wram[off + 2] << 16)
             | ((uint32_t)vb_wram[off + 3] << 24);
    }
    case 6: {
        uint32_t off = addr & VB_SRAM_MASK;
        return (uint32_t)vb_sram[off]
             | ((uint32_t)vb_sram[off + 1] << 8)
             | ((uint32_t)vb_sram[off + 2] << 16)
             | ((uint32_t)vb_sram[off + 3] << 24);
    }
    case 7: {
        if (!rom_ptr) return 0xFFFFFFFF;
        uint32_t off = addr & rom_mask;
        return (uint32_t)rom_ptr[off]
             | ((uint32_t)rom_ptr[off + 1] << 8)
             | ((uint32_t)rom_ptr[off + 2] << 16)
             | ((uint32_t)rom_ptr[off + 3] << 24);
    }
    default:
        return (uint32_t)vb_mem_read16(addr) | ((uint32_t)vb_mem_read16(addr + 2) << 16);
    }
}

void vb_mem_write8(vb_addr_t addr, uint8_t val) {
    switch (REGION(addr)) {
    case 0: vb_vip_write8(addr & 0x00FFFFFF, val); break;
    case 1: vb_vsu_write8(addr & 0x00FFFFFF, val); break;
    case 2: hw_write8(addr & 0x00FFFFFF, val); break;
    case 5: vb_wram[addr & VB_WRAM_MASK] = val; break;
    case 6: vb_sram[addr & VB_SRAM_MASK] = val; break;
    default: break;  /* ROM / unmapped: ignore writes */
    }
}

void vb_mem_write16(vb_addr_t addr, uint16_t val) {
    switch (REGION(addr)) {
    case 0: vb_vip_write16(addr & 0x00FFFFFF, val); break;
    case 5: {
        uint32_t off = addr & VB_WRAM_MASK;
        vb_wram[off]     = (uint8_t)(val);
        vb_wram[off + 1] = (uint8_t)(val >> 8);
        break;
    }
    case 6: {
        uint32_t off = addr & VB_SRAM_MASK;
        vb_sram[off]     = (uint8_t)(val);
        vb_sram[off + 1] = (uint8_t)(val >> 8);
        break;
    }
    default:
        vb_mem_write8(addr, (uint8_t)(val));
        vb_mem_write8(addr + 1, (uint8_t)(val >> 8));
        break;
    }
}

void vb_mem_write32(vb_addr_t addr, uint32_t val) {
    switch (REGION(addr)) {
    case 0: vb_vip_write32(addr & 0x00FFFFFF, val); break;
    case 5: {
        uint32_t off = addr & VB_WRAM_MASK;
        vb_wram[off]     = (uint8_t)(val);
        vb_wram[off + 1] = (uint8_t)(val >> 8);
        vb_wram[off + 2] = (uint8_t)(val >> 16);
        vb_wram[off + 3] = (uint8_t)(val >> 24);
        break;
    }
    case 6: {
        uint32_t off = addr & VB_SRAM_MASK;
        vb_sram[off]     = (uint8_t)(val);
        vb_sram[off + 1] = (uint8_t)(val >> 8);
        vb_sram[off + 2] = (uint8_t)(val >> 16);
        vb_sram[off + 3] = (uint8_t)(val >> 24);
        break;
    }
    default:
        vb_mem_write16(addr, (uint16_t)(val));
        vb_mem_write16(addr + 2, (uint16_t)(val >> 16));
        break;
    }
}
