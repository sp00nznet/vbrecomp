/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * mem.h - Memory bus
 *
 * Virtual Boy memory map (top nibble selects region):
 *   0x00000000 - 0x00FFFFFF  VIP (video registers + VRAM)
 *   0x01000000 - 0x01FFFFFF  VSU (sound registers + wave tables)
 *   0x02000000 - 0x02FFFFFF  Misc hardware (timer, gamepad, link, wait ctrl)
 *   0x03000000 - 0x03FFFFFF  Unmapped
 *   0x04000000 - 0x04FFFFFF  Cartridge expansion (unused by most games)
 *   0x05000000 - 0x05FFFFFF  WRAM (64KB, mirrored)
 *   0x06000000 - 0x06FFFFFF  Cartridge RAM/SRAM (8KB typical, mirrored)
 *   0x07000000 - 0x07FFFFFF  Cartridge ROM (mirrored)
 */

#ifndef VBRECOMP_MEM_H
#define VBRECOMP_MEM_H

#include "types.h"

/* WRAM: 64KB */
#define VB_WRAM_SIZE    0x10000
#define VB_WRAM_MASK    (VB_WRAM_SIZE - 1)

/* Cartridge SRAM: 8KB (common size, actual varies) */
#define VB_SRAM_SIZE    0x2000
#define VB_SRAM_MASK    (VB_SRAM_SIZE - 1)

/* Direct WRAM access for performance-critical paths */
extern uint8_t vb_wram[VB_WRAM_SIZE];

/* Direct SRAM access */
extern uint8_t vb_sram[VB_SRAM_SIZE];

/* Initialize memory subsystem with ROM data (for data reads) */
void vb_mem_init(const uint8_t *rom_data, uint32_t rom_size);
void vb_mem_shutdown(void);

/* Bus read/write - called by recompiled code for load/store instructions */
uint8_t  vb_mem_read8(vb_addr_t addr);
uint16_t vb_mem_read16(vb_addr_t addr);
uint32_t vb_mem_read32(vb_addr_t addr);
void vb_mem_write8(vb_addr_t addr, uint8_t val);
void vb_mem_write16(vb_addr_t addr, uint16_t val);
void vb_mem_write32(vb_addr_t addr, uint32_t val);

#endif /* VBRECOMP_MEM_H */
