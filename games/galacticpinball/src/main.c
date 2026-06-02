/*
 * Galactic Pinball - Static recompilation
 * SPDX-License-Identifier: MIT
 */

#include <vbrecomp/vbrecomp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../generated/recomp_funcs.h"

static uint32_t framebuffer[384 * 224];
static int frame_count = 0;

/* Per-frame callback: poll input, render VIP, present */
static bool on_frame(void) {
    frame_count++;

    if (!vb_platform_poll()) return false;
    vb_gamepad_set_buttons(vb_platform_get_buttons());

    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);

    /* Auto-screenshot every 200 frames during early bring-up */
    if (frame_count % 200 == 100) {
        char fname[64];
        snprintf(fname, sizeof(fname), "auto_%04d.png", frame_count);
        uint8_t *px = (uint8_t *)malloc(384 * 224 * 4);
        if (px) {
            for (int i = 0; i < 384 * 224; i++) {
                uint32_t c = framebuffer[i];
                px[i*4+0] = (c >> 24) & 0xFF;
                px[i*4+1] = 0;
                px[i*4+2] = 0;
                px[i*4+3] = c & 0xFF;
            }
            extern int stbi_write_png(const char *, int, int, int, const void *, int);
            stbi_write_png(fname, 384, 224, 4, px, 384 * 4);
            fprintf(stderr, "Auto-screenshot: %s\n", fname);
            free(px);
        }
    }

    if (frame_count % 60 == 1) {
        printf("Frame %d | DPCTRL:%04X\n",
               frame_count, vb_mem_read16(0x0005F822));
    }

    return true;
}

static uint8_t *load_rom(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Error: failed to read ROM\n");
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)size;
    return buf;
}

int main(int argc, char **argv) {
    const char *rom_path = argc > 1 ? argv[1] : "Galactic Pinball (Japan, USA).vb";

    uint32_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) return 1;

    printf("Galactic Pinball - vbrecomp\n");
    printf("ROM: %u bytes\n", rom_size);

    vb_init(rom, rom_size);

    /* The game's reset path (vb_func_07F3FF2C) sets SP/GP itself.
     * Just clear PSW and let it run. */
    vb_cpu.sr[VB_SREG_PSW] = 0;

    /* Interrupt handlers discovered by v810recomp:
     *   Timer (level 1): vb_func_07F00026
     *   VIP (level 4):   vb_func_07F3FF48 */
    vb_interrupt_set_handler(VB_INT_TIMER, vb_func_07F00026);
    vb_interrupt_set_handler(VB_INT_VIP,   vb_func_07F3FF48);

    vb_interrupt_set_frame_ticks(5000);
    vb_interrupt_set_frame_callback(on_frame);

    if (!vb_platform_init("Galactic Pinball - vbrecomp", 2)) {
        fprintf(stderr, "Failed to init SDL2 platform\n");
        free(rom);
        return 1;
    }

    printf("Starting game...\n");
    fflush(stdout);

    /* V810 reset vector at 0xFFFFFFF0. Runs the game's init path which
     * sets up SP/GP, configures VIP, fills WRAM with dispatch pointers,
     * and tail-calls the state-init function (vb_func_07F42D44 via the
     * hinted indirect jump at 07F42CDC). That function returns, which
     * normally would drop us out — on real hardware the per-frame
     * dispatcher (07F42F74 -> vb_func_07F5168C) is the actual main loop
     * via interrupt-driven state advancement. We drive it manually here. */
    fprintf(stderr, "Calling reset vector 07FFFFF0...\n");
    vb_func_07FFFFF0();
    fprintf(stderr, "Reset/init returned. SP=0x%08X GP=0x%08X r31=0x%08X\n",
            vb_cpu.r[3], vb_cpu.r[4], vb_cpu.r[31]);
    fprintf(stderr, "Entering per-frame loop (vb_func_07F5168C).\n");

    /* The per-frame dispatcher slot at [GP-0xCE3C] is a state machine:
     * each handler runs every frame and may transition by writing a
     * new pointer to that slot. Read the slot every frame and call
     * whichever known handler matches. */
    static uint32_t last_dispatch = 0;
    while (vb_platform_poll()) {
        uint32_t fp = vb_mem_read32(vb_cpu.r[4] + (int32_t)(int16_t)0xCE3C);
        if (fp >= 0xFFF00000) fp &= 0x07FFFFFF;

        if (fp != last_dispatch) {
            fprintf(stderr, "Per-frame dispatch -> 0x%08X (frame %d)\n", fp, frame_count);
            last_dispatch = fp;
        }

        switch (fp) {
            case 0x07F5168Cu: vb_func_07F5168C(); break;
            case 0x07F519D8u: vb_func_07F519D8(); break;
            case 0x07F519B8u: vb_func_07F519B8(); break;
            case 0x07F51A08u: vb_func_07F51A08(); break;
            default:
                fprintf(stderr, "Unknown per-frame target 0x%08X at frame %d, exiting\n",
                        fp, frame_count);
                goto exit_loop;
        }

        vb_vip_render(framebuffer, 0);
        vb_platform_present(framebuffer);
    }
exit_loop:;

    printf("Exited after %d frames\n", frame_count);

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
