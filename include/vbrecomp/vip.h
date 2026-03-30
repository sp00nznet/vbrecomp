/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * vip.h - Video Image Processor
 *
 * The VIP handles all graphics on the Virtual Boy:
 *   - 384x224 resolution per eye
 *   - 4 intensity levels (2-bit pixels)
 *   - Background layers via "worlds" (up to 32, 4 types)
 *   - 1024 objects (sprites) via OAM
 *   - Character (tile) based rendering, 8x8 tiles
 *   - Dual framebuffers for each eye
 *
 * VIP address space (within 0x00000000 - 0x00FFFFFF):
 *   0x00000000 - 0x00005FFF  Left framebuffer 0
 *   0x00006000 - 0x00007FFF  Character tables (CHR 0-1)
 *   0x00008000 - 0x0000DFFF  Left framebuffer 1
 *   0x0000E000 - 0x0000FFFF  Character tables (CHR 2-3)
 *   0x00010000 - 0x00015FFF  Right framebuffer 0
 *   0x00016000 - 0x00017FFF  (Mirror of CHR 0-1)
 *   0x00018000 - 0x0001DFFF  Right framebuffer 1
 *   0x0001E000 - 0x0001FFFF  (Mirror of CHR 2-3)
 *   0x00020000 - 0x0003D7FF  Background maps (BGMap 0-13)
 *   0x0003D800 - 0x0003DBFF  World attributes (31 worlds)
 *   0x0003DC00 - 0x0003DFFF  World attribute mirror
 *   0x0003E000 - 0x0003FFFF  OAM (Object Attribute Memory)
 *   0x00040000 - 0x0005FFFF  Mirror of above
 *   0x00060000 - 0x0007FFFF  VIP registers
 */

#ifndef VBRECOMP_VIP_H
#define VBRECOMP_VIP_H

#include "types.h"

/* VRAM total: 128KB (0x00000 - 0x3FFFF, mirrored at 0x40000) */
#define VB_VRAM_SIZE    0x40000

/* Framebuffer dimensions */
#define VB_SCREEN_WIDTH     384
#define VB_SCREEN_HEIGHT    224

/* VIP register offsets (from 0x0005F800) */
#define VB_VIP_REG_BASE     0x0005F800
#define VB_VIP_INTPND       0x00  /* Interrupt pending */
#define VB_VIP_INTENB       0x02  /* Interrupt enable */
#define VB_VIP_INTCLR       0x04  /* Interrupt clear */
#define VB_VIP_DPSTTS       0x10  /* Display status */
#define VB_VIP_DPCTRL       0x12  /* Display control */
#define VB_VIP_BRTA         0x14  /* Brightness A */
#define VB_VIP_BRTB         0x16  /* Brightness B */
#define VB_VIP_BRTC         0x18  /* Brightness C */
#define VB_VIP_REST          0x1A  /* Rest value */
#define VB_VIP_FRMCYC       0x1C  /* Frame repeat */
#define VB_VIP_CTA          0x1E  /* Column table index */
#define VB_VIP_XPSTTS       0x20  /* Drawing status */
#define VB_VIP_XPCTRL       0x22  /* Drawing control */
#define VB_VIP_SPT0         0x24  /* OBJ group 0 end */
#define VB_VIP_SPT1         0x26  /* OBJ group 1 end */
#define VB_VIP_SPT2         0x28  /* OBJ group 2 end */
#define VB_VIP_SPT3         0x2A  /* OBJ group 3 end */
#define VB_VIP_GPLT0        0x30  /* Background palette 0 */
#define VB_VIP_GPLT1        0x32  /* Background palette 1 */
#define VB_VIP_GPLT2        0x34  /* Background palette 2 */
#define VB_VIP_GPLT3        0x36  /* Background palette 3 */
#define VB_VIP_JPLT0        0x38  /* OBJ palette 0 */
#define VB_VIP_JPLT1        0x3A  /* OBJ palette 1 */
#define VB_VIP_JPLT2        0x3C  /* OBJ palette 2 */
#define VB_VIP_JPLT3        0x3E  /* OBJ palette 3 */
#define VB_VIP_BKCOL        0x40  /* Background color */

/* Interrupt bits */
#define VB_VIP_INT_SCANERR  (1 << 0)   /* Scan error */
#define VB_VIP_INT_LFBEND   (1 << 1)   /* Left framebuffer draw end */
#define VB_VIP_INT_RFBEND   (1 << 2)   /* Right framebuffer draw end */
#define VB_VIP_INT_GAMESTART (1 << 3)  /* Game frame start */
#define VB_VIP_INT_FRAMESTART (1 << 4) /* Frame start */
#define VB_VIP_INT_XPEND    (1 << 14)  /* Drawing finished */

void vb_vip_init(void);

/* Memory/register access from the bus */
uint8_t  vb_vip_read8(vb_addr_t addr);
uint16_t vb_vip_read16(vb_addr_t addr);
uint32_t vb_vip_read32(vb_addr_t addr);
void vb_vip_write8(vb_addr_t addr, uint8_t val);
void vb_vip_write16(vb_addr_t addr, uint16_t val);
void vb_vip_write32(vb_addr_t addr, uint32_t val);

/*
 * Render current VIP state to an RGBA8888 framebuffer.
 * The output buffer must hold VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT * 4 bytes.
 * eye: 0 = left, 1 = right
 */
void vb_vip_render(uint32_t *out_rgba, int eye);

/* Advance VIP state (call once per frame to update status registers) */
void vb_vip_frame_advance(void);

#endif /* VBRECOMP_VIP_H */
