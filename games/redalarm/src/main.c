/*
 * Red Alarm - Static recompilation
 * SPDX-License-Identifier: MIT
 */

#include <vbrecomp/vbrecomp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../generated/recomp_funcs.h"

static uint32_t framebuffer[384 * 224];
static int frame_count = 0;
static bool on_frame(void);

/*
 * HLE replacement for vb_func_07F20716 — the frame sync / VBlank wait.
 *
 * Original code:
 *   1. Clears [r4+0x82C0] to 0
 *   2. Calls vb_func_07F2075E (sets brightness registers)
 *   3. Polls [r4+0x82C0] until it reaches r1 (passed in r27)
 *      (VIP interrupt handler -> vb_func_07F2084A increments [r4+0x82C0])
 *   4. Checks [r4+0xE318], loops back if nonzero
 *   5. Returns
 *
 * Our HLE: advance N frames (r1 count), calling the VIP interrupt
 * handler each frame to update game state.
 */
void vb_func_07F20716(void) {
    static int call_count = 0;
    call_count++;
    uint32_t wait_frames = vb_cpu.r[1];
    if (call_count <= 20) {
        fprintf(stderr, "HLE vb_func_07F20716 called (count=%d, wait=%d, frame=%d)\n",
                call_count, wait_frames, frame_count);
    }
    if (wait_frames == 0) wait_frames = 1;
    if (wait_frames > 120) wait_frames = 1; /* sanity cap */

    for (uint32_t i = 0; i < wait_frames; i++) {
        /* Advance VIP state */
        vb_vip_frame_advance();

        /* Manually increment the frame counter that the original code polls.
         * The VIP interrupt handler (vb_func_07F207E8) normally does this via
         * vb_func_07F2084A which increments [r4+0x82C0].
         * r4 = GP = 0x05008000, so the address is 0x050102C0. */
        uint32_t gp = vb_cpu.r[4];
        uint16_t fc = vb_mem_read16(gp + (int32_t)(int16_t)0x82C0);
        vb_mem_write16(gp + (int32_t)(int16_t)0x82C0, fc + 1);

        /* Also increment the 32-bit frame counter at [r4+0x82F0] */
        uint32_t fc32 = vb_mem_read32(gp + (int32_t)(int16_t)0x82F0);
        vb_mem_write32(gp + (int32_t)(int16_t)0x82F0, fc32 + 1);

        /* Re-enable VIP interrupts (handler clears INTENB) */
        vb_mem_write16(0x0005F802, 0x0010);

        /* Render and present */
        on_frame();
    }
}

/*
 * HLE replacement for vb_func_07F1EB38 — XPSTTS wait.
 *
 * Original polls XPSTTS (0x0005F820) bit 0x0040 until set.
 * Drawing is instantaneous for us, so just return.
 */
void vb_func_07F1EB38(void) {
    static int xp_count = 0;
    xp_count++;
    if (xp_count <= 5) {
        fprintf(stderr, "XPSTTS_WAIT called (count=%d, frame=%d)\n", xp_count, frame_count);
    }
}

/*
 * HLE for vb_func_07F3C9CE — per-frame render init.
 * Original sets [r4+0x8045]=1, [r4+0x8044]|=1, then polls until
 * bit 0 is cleared by the VIP interrupt's render path.
 * We skip the polling and do the remaining setup directly.
 */
extern void vb_func_07F3C9CE_real(void);
void vb_func_07F3C9CE(void) {
    uint32_t gp = vb_cpu.r[4];

    /* Set rendering-active flag (like original) */
    vb_mem_write8(gp + (int32_t)(int16_t)0x8045, 1);

    /* Skip the [r4+0x8044] polling loop entirely.
     * On real HW, the interrupt handler clears this after rendering.
     * We just leave it clear. */
    vb_mem_write8(gp + (int32_t)(int16_t)0x8044, 0);
    vb_mem_write8(gp + (int32_t)(int16_t)0x804B, 0);
    vb_mem_write8(gp + (int32_t)(int16_t)0x804C, 0);

    /* Do the post-poll setup (label_07F3CA0C onwards) */
    vb_mem_write16(gp + (int32_t)(int16_t)0x82CE, 1);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82F8, 0);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82D2, 0);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82D4, 0);

    /* Call the remaining setup functions */
    vb_func_07F209D8();

    /* Set viewport parameters */
    vb_mem_write16(gp + (int32_t)(int16_t)0x07A6, 0x0580);
    vb_mem_write16(gp + (int32_t)(int16_t)0x07AA, 0x0640);
    vb_mem_write16(gp + (int32_t)(int16_t)0x07AE, 0x0480);
    vb_mem_write16(gp + (int32_t)(int16_t)0x07BC, 0x0580);
    vb_mem_write16(gp + (int32_t)(int16_t)0x07BE, 0x0640);
    vb_mem_write16(gp + (int32_t)(int16_t)0x07C0, 0x0480);

    vb_func_07F23274();
    vb_func_07F233AE();
    vb_func_07F38760();

    /* Set brightness and clear frame counter */
    vb_mem_write16(gp + (int32_t)(int16_t)0xE318, 0);
    vb_func_07F2075E();

    /* Call vb_func_07F2C4BE with r1=8 (level/state select?) */
    vb_cpu.r[1] = 8;
    vb_func_07F2C4BE();

    /* Process return value */
    vb_cpu.r[30] = vb_cpu.r[30] << 2;
    vb_cpu.r[1] = vb_cpu.r[30] + 0x07030000;
    vb_cpu.r[6] = vb_mem_read32(vb_cpu.r[1] + (int32_t)(int16_t)0x7898);
    vb_func_07F22DF0();

    vb_mem_write8(gp + (int32_t)(int16_t)0x82D7, 0);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82D0, 0);
}

/* Init function tracing wrappers */
extern void vb_func_07F20488_real(void);
void vb_func_07F20488(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F20488 (decompression)\n");
    vb_func_07F20488_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F20488\n");
}

extern void vb_func_07F22AFE_real(void);
void vb_func_07F22AFE(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F22AFE\n");
    vb_func_07F22AFE_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F22AFE\n");
}

extern void vb_func_07F0002C_real(void);
void vb_func_07F0002C(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F0002C\n");
    vb_func_07F0002C_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F0002C\n");
}

/*
 * HLE for vb_func_07F202AE — core "wait N frames + start drawing" primitive.
 * r1 = number of frames to wait. Polls [r4+0x82C0] then waits for XPSTTS.
 * Called directly from game loops (not just via vb_func_07F20292/07F20716).
 */
/*
 * Helper: advance one frame with manual state updates.
 * Updates all the flags the interrupt handler would normally set.
 */
static void advance_frame(void) {
    uint32_t gp = vb_cpu.r[4];

    vb_vip_frame_advance();

    /* Increment frame counters */
    uint16_t fc = vb_mem_read16(gp + (int32_t)(int16_t)0x82C0);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82C0, fc + 1);
    uint32_t fc32 = vb_mem_read32(gp + (int32_t)(int16_t)0x82F0);
    vb_mem_write32(gp + (int32_t)(int16_t)0x82F0, fc32 + 1);

    /* Increment animation frame lag counter [r4+0xE318] */
    uint16_t lag = vb_mem_read16(gp + (int32_t)(int16_t)0xE318);
    vb_mem_write16(gp + (int32_t)(int16_t)0xE318, lag + 1);

    /* Clear rendering-complete flag */
    vb_mem_write8(gp + (int32_t)(int16_t)0x8044, 0);

    /* Increment per-frame byte counter at [r4+0x029A] */
    uint8_t pfb = vb_mem_read8(gp + (int32_t)(int16_t)0x029A);
    vb_mem_write8(gp + (int32_t)(int16_t)0x029A, pfb + 1);

    /* Update gamepad state */
    uint16_t buttons = vb_platform_get_buttons();
    if (frame_count >= 60 && frame_count <= 80) {
        buttons |= 0x0004;  /* Auto Start for logos */
    }
    uint16_t prev = vb_mem_read16(gp + (int32_t)(int16_t)0x82FC);
    uint16_t changed = prev ^ buttons;
    uint16_t pressed = changed & buttons;
    vb_mem_write16(gp + (int32_t)(int16_t)0x82FC, buttons);
    uint16_t acc1 = vb_mem_read16(gp + (int32_t)(int16_t)0x8304);
    uint16_t acc2 = vb_mem_read16(gp + (int32_t)(int16_t)0x8306);
    vb_mem_write16(gp + (int32_t)(int16_t)0x8304, acc1 | buttons);
    vb_mem_write16(gp + (int32_t)(int16_t)0x8306, acc2 | pressed);

    vb_mem_write16(0x0005F802, 0x0010);
    on_frame();
}

void vb_func_07F202AE(void) {
    uint32_t wait_frames = vb_cpu.r[1];
    if (wait_frames == 0) wait_frames = 1;
    if (wait_frames > 120) wait_frames = 1;

    for (uint32_t i = 0; i < wait_frames; i++) {
        advance_frame();
    }

    uint32_t gp = vb_cpu.r[4];
    /* Clear frame counter after wait (like original) */
    vb_mem_write16(gp + (int32_t)(int16_t)0x82C0, 0);
    /* Start drawing: set XPCTRL bit 1 */
    uint16_t xpctrl = vb_mem_read16(vb_cpu.r[5] + (int32_t)(int16_t)0x7840);
    vb_mem_write16(vb_cpu.r[5] + (int32_t)(int16_t)0x7842, xpctrl | 0x0002);
    /* Set r30 to nonzero (XPSTTS bits the caller might check) */
    vb_cpu.r[30] = 0x000C;
}

/*
 * HLE for vb_func_07F20292 — "wait one frame + XPCTRL draw start"
 * Clears frame counter, calls vb_func_07F202AE (wait loop), then
 * starts drawing by writing XPCTRL. We HLE the whole thing.
 */
void vb_func_07F20292(void) {
    static int call_count = 0;
    call_count++;
    if (call_count <= 5) {
        fprintf(stderr, "HLE vb_func_07F20292 called (count=%d, frame=%d)\n", call_count, frame_count);
    }
    /* Clear and advance frame counter */
    uint32_t gp = vb_cpu.r[4];
    vb_mem_write16(gp + (int32_t)(int16_t)0x82C0, 0);
    vb_mem_write32(gp + (int32_t)(int16_t)0x020C, 0);

    /* Advance one frame */
    vb_vip_frame_advance();
    uint16_t fc = vb_mem_read16(gp + (int32_t)(int16_t)0x82C0);
    vb_mem_write16(gp + (int32_t)(int16_t)0x82C0, fc + 1);
    uint32_t fc32 = vb_mem_read32(gp + (int32_t)(int16_t)0x82F0);
    vb_mem_write32(gp + (int32_t)(int16_t)0x82F0, fc32 + 1);

    /* Start drawing: set XPCTRL bits (like the original does after the wait) */
    uint16_t xpctrl = vb_mem_read16(vb_cpu.r[5] + (int32_t)(int16_t)0x7840);
    vb_mem_write16(vb_cpu.r[5] + (int32_t)(int16_t)0x7842, xpctrl | 0x0002);

    vb_mem_write16(0x0005F802, 0x0010); /* re-enable VIP interrupts */
    on_frame();
}

extern void vb_func_07F204D0_real(void);
void vb_func_07F204D0(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F204D0\n");
    vb_func_07F204D0_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F204D0\n");
}

extern void vb_func_07F204AA_real(void);
void vb_func_07F204AA(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F204AA\n");
    vb_func_07F204AA_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F204AA\n");
}

extern void vb_func_07F1A434_real(void);
void vb_func_07F1A434(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F1A434\n");
    vb_func_07F1A434_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F1A434\n");
}

extern void vb_func_07F1DC54_real(void);
void vb_func_07F1DC54(void) {
    fprintf(stderr, "TRACE: entering vb_func_07F1DC54\n");
    vb_func_07F1DC54_real();
    fprintf(stderr, "TRACE: returned from vb_func_07F1DC54\n");
}

extern void vb_func_07F3C580_real(void);
void vb_func_07F3C580(void) {
    static int call_count = 0;
    call_count++;

    /* Advance one frame with manual state updates */
    advance_frame();

    /* Copy accumulated button state */
    vb_func_07F20368();

    vb_func_07F3C580_real();
}

/* Called once per frame from within the game loop */
static bool on_frame(void) {
    frame_count++;

    /* Poll SDL events and update gamepad */
    if (!vb_platform_poll()) return false;
    vb_gamepad_set_buttons(vb_platform_get_buttons());

    /* Render VIP to framebuffer and present */
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);

    /* Auto-screenshot at key frames */
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

    /* Log state periodically */
    if (frame_count % 60 == 1) {
        /* Check VRAM usage including frame buffers */
        int fb_nonzero = 0, chr_nonzero = 0, bg_nonzero = 0, wld_nonzero = 0;
        for (int i = 0; i < 0x40000; i += 4) {
            uint32_t v = vb_mem_read32(i);
            if (v == 0) continue;
            if (i < 0x06000 || (i >= 0x08000 && i < 0x0E000) ||
                (i >= 0x10000 && i < 0x16000) || (i >= 0x18000 && i < 0x1E000))
                fb_nonzero++;
            else if ((i >= 0x06000 && i < 0x08000) || (i >= 0x0E000 && i < 0x10000) ||
                (i >= 0x16000 && i < 0x18000) || (i >= 0x1E000 && i < 0x20000))
                chr_nonzero++;
            else if (i >= 0x20000 && i < 0x3D800) bg_nonzero++;
            else if (i >= 0x3D800 && i < 0x3E000) wld_nonzero++;
        }
        printf("Frame %d | FB:%d CHR:%d BG:%d WLD:%d | DPCTRL:%04X\n",
               frame_count, fb_nonzero, chr_nonzero, bg_nonzero, wld_nonzero,
               vb_mem_read16(0x0005F822));
    }

    return true;
}

/* Read file into malloc'd buffer */
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
    const char *rom_path = argc > 1 ? argv[1] : "Red Alarm (USA).vb";

    /* Load ROM for data access */
    uint32_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) return 1;

    printf("Red Alarm - vbrecomp\n");
    printf("ROM: %u bytes\n", rom_size);

    /* Initialize all VB subsystems */
    vb_init(rom, rom_size);

    /* Set up initial CPU state */
    vb_cpu.r[3] = 0x05003FFC;  /* SP: top of WRAM */
    vb_cpu.r[4] = 0x05008000;  /* GP */
    vb_cpu.r[5] = 0x00058000;  /* VIP register base (r5 used as VIP base in Red Alarm) */

    /* Clear PSW */
    vb_cpu.sr[VB_SREG_PSW] = 0;

    /* Register interrupt handlers */
    vb_interrupt_set_handler(VB_INT_TIMER, vb_func_07F207CC);
    vb_interrupt_set_handler(VB_INT_LINK,  vb_func_07F207C4);
    vb_interrupt_set_handler(VB_INT_VIP,   vb_func_07F207E8);

    /* Disable frame ticks during init */
    vb_interrupt_set_frame_ticks(0);

    /* Set up frame timing and callback */
    vb_interrupt_set_frame_ticks(5000);
    vb_interrupt_set_frame_callback(on_frame);

    /* Initialize SDL2 window */
    if (!vb_platform_init("Red Alarm - vbrecomp", 2)) {
        fprintf(stderr, "Failed to init SDL2 platform\n");
        free(rom);
        return 1;
    }

    printf("Starting game...\n");
    fflush(stdout);

    /* Run entry point — this contains the game's infinite main loop,
     * so it should never return. The frame sync function (vb_func_07F20716)
     * inside the loop is HLE'd to advance frames. */
    fprintf(stderr, "Calling entry point 07F045D4...\n");
    vb_func_07F045D4();
    printf("Entry point returned! SP=0x%08X r31=0x%08X\n",
           vb_cpu.r[3], vb_cpu.r[31]);

    /* Force-enable display */
    vb_mem_write16(0x0005F822, 0x0302);

    /* Force-set palettes */
    vb_mem_write16(0x0005F860, 0xE4); /* GPLT0 */
    vb_mem_write16(0x0005F862, 0xE4); /* GPLT1 */
    vb_mem_write16(0x0005F864, 0xE4); /* GPLT2 */
    vb_mem_write16(0x0005F866, 0xE4); /* GPLT3 */
    vb_mem_write16(0x0005F868, 0xE4); /* JPLT0 */
    vb_mem_write16(0x0005F86A, 0xE4); /* JPLT1 */
    vb_mem_write16(0x0005F86C, 0xE4); /* JPLT2 */
    vb_mem_write16(0x0005F86E, 0xE4); /* JPLT3 */

    /* Render and display loop */
    printf("Entering render loop...\n");
    while (vb_platform_poll()) {
        vb_vip_render(framebuffer, 0);
        vb_platform_present(framebuffer);
    }

    printf("Game exited after %d frames\n", frame_count);

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
