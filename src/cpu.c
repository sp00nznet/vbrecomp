/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 */

#include "vbrecomp/cpu.h"
#include <string.h>

vb_cpu_state_t vb_cpu;

void vb_cpu_init(void) {
    memset(&vb_cpu, 0, sizeof(vb_cpu));
    /* r0 is always zero - enforced by convention */
    vb_cpu.r[0] = 0;
    /* PIR: processor ID for V810 */
    vb_cpu.sr[VB_SREG_PIR] = 0x00005346;
    /* Initial PSW: interrupts disabled, NMI pending */
    vb_cpu.sr[VB_SREG_PSW] = VB_PSW_NP | VB_PSW_ID;
    /* PC starts at reset vector */
    vb_cpu.pc = 0xFFFFFFF0;
}
