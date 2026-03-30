/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Top-level init/shutdown.
 */

#include "vbrecomp/vbrecomp.h"

void vb_init(const uint8_t *rom_data, uint32_t rom_size) {
    vb_cpu_init();
    vb_mem_init(rom_data, rom_size);
    vb_vip_init();
    vb_vsu_init();
    vb_timer_init();
    vb_gamepad_init();
    vb_interrupt_init();
}

void vb_shutdown(void) {
    vb_mem_shutdown();
}
