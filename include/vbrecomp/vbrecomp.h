/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * vbrecomp.h - Master include
 */

#ifndef VBRECOMP_H
#define VBRECOMP_H

#include "types.h"
#include "cpu.h"
#include "mem.h"
#include "vip.h"
#include "vsu.h"
#include "timer.h"
#include "gamepad.h"
#include "interrupt.h"
#include "platform.h"

/* Initialize all subsystems. Call before anything else. */
void vb_init(const uint8_t *rom_data, uint32_t rom_size);

/* Shutdown all subsystems. */
void vb_shutdown(void);

#endif /* VBRECOMP_H */
