/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * vsu.h - Virtual Sound Unit
 *
 * The VSU has 6 sound channels:
 *   - Channels 1-5: Wavetable (32 samples, 6-bit each)
 *   - Channel 5 also supports sweep/modulation
 *   - Channel 6: Noise generator
 * Each channel has frequency, envelope, and stereo level controls.
 *
 * VSU address space (within 0x01000000 - 0x01FFFFFF):
 *   0x01000000 - 0x010003FF  Wave tables (5 x 128 bytes)
 *   0x01000400 - 0x010005FF  Modulation table
 *   0x01000600 - 0x010007FF  Channel registers
 */

#ifndef VBRECOMP_VSU_H
#define VBRECOMP_VSU_H

#include "types.h"

void vb_vsu_init(void);

/* Register/memory access from the bus */
uint8_t vb_vsu_read8(vb_addr_t addr);
void vb_vsu_write8(vb_addr_t addr, uint8_t val);

/*
 * Generate audio samples.
 * Fills buffer with interleaved signed 16-bit stereo PCM.
 * num_samples is the number of stereo sample pairs.
 */
void vb_vsu_generate(int16_t *buffer, int num_samples, int sample_rate);

#endif /* VBRECOMP_VSU_H */
