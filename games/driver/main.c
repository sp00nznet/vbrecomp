/*
 * Generic vbrecomp driver.
 *
 * Boots any v810recomp-generated game with no game-specific glue: it loads the
 * ROM, registers the interrupt handlers the recompiler discovered, and runs the
 * reset vector. A real game's infinite main loop keeps calling vb_interrupt_check()
 * (emitted at every function entry), which drives the per-frame callback below.
 *
 * Games whose reset path returns instead of looping (or that need a custom
 * dispatch) get a hand-written main.c instead — see e.g. games/galacticpinball.
 *
 * Build via vb_add_game_generic() in games/CMakeLists.txt. The game's
 * generated/ dir is on the include path so "recomp_funcs.h" resolves.
 *
 * SPDX-License-Identifier: MIT
 */

#include <vbrecomp/vbrecomp.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_funcs.h"

static uint32_t framebuffer[VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT];
static int frame_count;
static int max_frames;   /* 0 = unlimited (VBRECOMP_HEADLESS_FRAMES) */

/* Per-frame: poll input, render, present. Driven by vb_interrupt_check(). */
static bool on_frame(void) {
    frame_count++;
    if (!vb_platform_poll()) exit(0);
    vb_gamepad_set_buttons(vb_platform_get_buttons());
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);
    if (max_frames && frame_count >= max_frames) exit(0);
    return true;
}

static uint8_t *load_rom(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(size);
    if (!buf || fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Error: failed to read ROM\n");
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)size;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <rom.vb>\n", argv[0]); return 1; }

    uint32_t rom_size;
    uint8_t *rom = load_rom(argv[1], &rom_size);
    if (!rom) return 1;
    printf("vbrecomp generic driver - ROM: %u bytes\n", rom_size);

    const char *mf = getenv("VBRECOMP_HEADLESS_FRAMES");
    max_frames = mf ? atoi(mf) : 0;

    vb_init(rom, rom_size);
    vb_cpu.sr[VB_SREG_PSW] = 0;
    vb_recomp_init_handlers();
    vb_interrupt_set_frame_callback(on_frame);
    vb_interrupt_set_frame_ticks(20000);

    if (!vb_platform_init("vbrecomp", 2)) {
        fprintf(stderr, "Failed to init platform\n");
        free(rom); return 1;
    }

    /* Boot. If the game loops forever (the common case), on_frame drives
     * rendering until the user quits / the frame budget is hit (via exit()).
     * If the reset path returns, fall through to a manual render loop. */
    vb_recomp_boot();
    fprintf(stderr, "Reset returned after %d frames; entering fallback loop.\n", frame_count);
    while (vb_platform_poll()) {
        vb_gamepad_set_buttons(vb_platform_get_buttons());
        vb_vip_render(framebuffer, 0);
        vb_platform_present(framebuffer);
        if (max_frames && ++frame_count >= max_frames) break;
    }

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
