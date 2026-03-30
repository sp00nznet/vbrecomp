/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * types.h - Common type definitions
 */

#ifndef VBRECOMP_TYPES_H
#define VBRECOMP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* VB address space is 32-bit */
typedef uint32_t vb_addr_t;

/* VB button bits (active high in our representation) */
#define VB_BTN_PWR   (1 << 0)   /* Not a real button, but mapped in hardware */
#define VB_BTN_SGN   (1 << 1)   /* Signal (active low = battery OK) */
#define VB_BTN_A     (1 << 2)
#define VB_BTN_B     (1 << 3)
#define VB_BTN_RT    (1 << 4)   /* Right trigger */
#define VB_BTN_LT    (1 << 5)   /* Left trigger */
#define VB_BTN_RU    (1 << 6)   /* Right pad up */
#define VB_BTN_RR    (1 << 7)   /* Right pad right */
#define VB_BTN_LR    (1 << 8)   /* Left pad right */
#define VB_BTN_LL    (1 << 9)   /* Left pad left */
#define VB_BTN_LD    (1 << 10)  /* Left pad down */
#define VB_BTN_LU    (1 << 11)  /* Left pad up */
#define VB_BTN_STA   (1 << 12)  /* Start */
#define VB_BTN_SEL   (1 << 13)  /* Select */
#define VB_BTN_RL    (1 << 14)  /* Right pad left */
#define VB_BTN_RD    (1 << 15)  /* Right pad down */

#endif /* VBRECOMP_TYPES_H */
