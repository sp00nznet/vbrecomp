/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * platform.h - Platform abstraction (windowing, audio, input)
 */

#ifndef VBRECOMP_PLATFORM_H
#define VBRECOMP_PLATFORM_H

#include "types.h"

/* Initialize platform (window + audio). Returns false on failure. */
bool vb_platform_init(const char *title, int scale);

/* Present an RGBA8888 framebuffer (384x224) to the window */
void vb_platform_present(const uint32_t *framebuffer);

/* Poll events. Returns false if the user requested quit. */
bool vb_platform_poll(void);

/* Get current button state (VB_BTN_* bits) */
uint16_t vb_platform_get_buttons(void);

/* Audio callback type: fill buffer with num_samples stereo pairs */
typedef void (*vb_audio_callback_t)(int16_t *buffer, int num_samples);

void vb_platform_audio_start(vb_audio_callback_t callback);
void vb_platform_audio_stop(void);

/* Cleanup */
void vb_platform_shutdown(void);

#endif /* VBRECOMP_PLATFORM_H */
