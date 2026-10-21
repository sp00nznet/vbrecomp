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
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "recomp_funcs.h"

static uint32_t framebuffer[VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT];
static int frame_count;
static int max_frames;          /* 0 = unlimited (VBRECOMP_HEADLESS_FRAMES) */
static const char *shot_path;   /* VBRECOMP_SHOT_PATH: dump a PNG before exit */
static int shot_every;          /* VBRECOMP_SHOT_EVERY: also dump every N frames */
/* VBRECOMP_PRESS: hex button masks to auto-press, comma-separated, one per
 * burst in turn ("1000,0004" = Start, then A, then Start...). Press ONE button
 * at a time: holding A|B|L|R|Start|Select together (the old 303C default) is a
 * soft-reset combo, and 3-D Tetris and Teleroboxer restarted to their warning
 * screen on every burst. */
static uint16_t inject_btn[16];
static int inject_n;

extern int stbi_write_png(const char *, int, int, int, const void *, int);

/* Dump the current framebuffer (RGBA8888) to a PNG. */
static void dump_png(const char *path) {
    static uint8_t px[VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT * 4];
    if (getenv("VBRECOMP_HEARTBEAT")) {
        int nzfb = 0;
        for (int i = 0; i < VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT; i++) if (framebuffer[i] >> 24) nzfb++;
        fprintf(stderr, "dump_png %s: %d non-zero framebuffer pixels\n", path, nzfb);
    }
    for (int i = 0; i < VB_SCREEN_WIDTH * VB_SCREEN_HEIGHT; i++) {
        uint32_t c = framebuffer[i];
        px[i*4+0] = (c >> 24) & 0xFF;  /* R */
        px[i*4+1] = (c >> 16) & 0xFF;  /* G */
        px[i*4+2] = (c >> 8) & 0xFF;   /* B */
        px[i*4+3] = 0xFF;              /* A */
    }
    stbi_write_png(path, VB_SCREEN_WIDTH, VB_SCREEN_HEIGHT, 4, px, VB_SCREEN_WIDTH * 4);
}

/* --record: raw frames piped to ffmpeg. Our RGBA8888 words are 0xRRGGBBAA, so
 * in little-endian memory the bytes are A,B,G,R -- ffmpeg's "abgr". */
static FILE *rec_pipe;

static void close_record(void) {
    if (!rec_pipe) return;
#ifdef _WIN32
    _pclose(rec_pipe);
#else
    pclose(rec_pipe);
#endif
    rec_pipe = NULL;
}

static void open_record(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -loglevel error -f rawvideo -pix_fmt abgr -s %dx%d -r 50.27 -i - "
        "-c:v libx264 -pix_fmt yuv420p \"%s\"", VB_SCREEN_WIDTH, VB_SCREEN_HEIGHT, path);
#ifdef _WIN32
    rec_pipe = _popen(cmd, "wb");
#else
    rec_pipe = popen(cmd, "w");
#endif
    if (!rec_pipe) { fprintf(stderr, "could not start ffmpeg for --record (is it on PATH?)\n"); return; }
    atexit(close_record);   /* the frame budget ends the run through exit() */
}

static void set_env(const char *k, const char *v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

static void finish(void) {
    if (shot_path) dump_png(shot_path);
    exit(0);
}

/* Per-frame: poll input, render, present. Driven by vb_interrupt_check(). */
static bool on_frame(void) {
    frame_count++;
    if (!vb_platform_poll()) finish();
    {
        uint16_t btn = vb_platform_get_buttons();
        /* Test aid: auto-press a button in periodic bursts (press then release),
         * so a headless run can get past "press start"-style gates. */
        if (inject_n && frame_count > 60 && (frame_count % 120) < 40)
            btn |= inject_btn[(frame_count / 120) % inject_n];
        vb_gamepad_set_buttons(btn);
    }
    vb_vip_render(framebuffer, 0);
    vb_platform_present(framebuffer);
    if (rec_pipe) fwrite(framebuffer, sizeof(framebuffer), 1, rec_pipe);
    /* VBRECOMP_WRAM_DUMP=<path> with VBRECOMP_WRAM_FRAME=<n>: write the whole
     * 64 KB of WRAM at frame n, to diff against the same dump taken from real
     * hardware (scripts/reference/dumpwram.lua). Knowing a game loads tiles by
     * frame n on hardware says WHEN we diverge; the first differing block says
     * WHERE. */
    {
        const char *wd = getenv("VBRECOMP_WRAM_DUMP");
        const char *wf = getenv("VBRECOMP_WRAM_FRAME");
        if (wd && wd[0] && frame_count == (wf ? atoi(wf) : 10)) {
            FILE *w = fopen(wd, "wb");
            if (w) { fwrite(vb_wram, 1, VB_WRAM_SIZE, w); fclose(w); }
        }
    }

    if (shot_every > 0 && shot_path && frame_count % shot_every == 0) {
        char p[512];
        snprintf(p, sizeof(p), "%s.%04d.png", shot_path, frame_count);
        dump_png(p);
    }
    if (max_frames && frame_count >= max_frames) finish();
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
    /* House-style flags (same names as snesrecomp). They only set the env knobs
     * the rest of the runtime already reads, so the two can't disagree. */
    const char *rom_path = NULL, *record = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) set_env("VBRECOMP_HEADLESS", "1");
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) { record = argv[++i]; set_env("VBRECOMP_HEADLESS", "1"); }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) set_env("VBRECOMP_HEADLESS_FRAMES", argv[++i]);
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "usage: %s [--headless] [--record out.mp4] [--frames N] <rom.vb>\n", argv[0]);
        return 1;
    }

    uint32_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) return 1;
    printf("vbrecomp generic driver - ROM: %u bytes\n", rom_size);

    const char *mf = getenv("VBRECOMP_HEADLESS_FRAMES");
    max_frames = mf ? atoi(mf) : 0;
    shot_path = getenv("VBRECOMP_SHOT_PATH");
    if (shot_path && !shot_path[0]) shot_path = NULL;
    const char *se = getenv("VBRECOMP_SHOT_EVERY");
    shot_every = se ? atoi(se) : 0;
    const char *pb = getenv("VBRECOMP_PRESS");
    for (char *e = (char *)pb; pb && *e && inject_n < 16; ) {
        uint16_t m = (uint16_t)strtoul(e, &e, 16);
        if (m) inject_btn[inject_n++] = m;
        if (*e == ',') e++; else break;
    }

    vb_init(rom, rom_size);
    vb_cpu.sr[VB_SREG_PSW] = 0;
    vb_recomp_init_handlers();
    vb_interrupt_set_frame_callback(on_frame);
    /* How many interrupt checks make a frame -- i.e. how much CPU time the game
     * gets per rendered frame. A game that waits on a timing relationship
     * between its main loop and the VIP is sensitive to this, and 20000 is a
     * guess that has never been varied. VBRECOMP_FRAME_TICKS makes it sweepable.
     */
    {
        const char *ft = getenv("VBRECOMP_FRAME_TICKS");
        vb_interrupt_set_frame_ticks(ft ? (uint32_t)strtoul(ft, NULL, 10) : 20000);
    }

    if (!vb_platform_init("vbrecomp", 2)) {
        fprintf(stderr, "Failed to init platform\n");
        free(rom); return 1;
    }
    if (record) open_record(record);

    /* Boot. If the game loops forever (the common case), on_frame drives
     * rendering until the user quits / the frame budget is hit (via exit()).
     * If the reset path returns, fall through to a manual render loop. */
    /* A recompiled game that runs off the end of the C stack (a control-flow
     * cycle translated as calls) otherwise just vanishes: the process dies with
     * STATUS_STACK_OVERFLOW and prints nothing, which is how Galactic Pinball
     * and 3-D Tetris read as "blank" for a long time. __except unwinds before
     * the handler runs, so it is safe to print here -- and the trace ring names
     * the functions that were cycling. */
#ifdef _WIN32
    __try {
        vb_recomp_boot();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "CRASH: exception 0x%08lX after %d frames\n",
                (unsigned long)GetExceptionCode(), frame_count);
        vb_recomp_dump_trace();
        vb_recomp_dump_stubs();
        fflush(stderr);
        if (shot_path) dump_png(shot_path);
        return 2;
    }
#else
    vb_recomp_boot();
#endif

    /* If the reset path returned, the game is interrupt-driven (its per-frame
     * work runs in the VIP/timer interrupt handlers, not an explicit loop).
     * Keep driving vb_interrupt_check() so those handlers fire; the per-frame
     * callback (on_frame) does render/present/poll and exits at the frame
     * budget or on quit. Games whose reset loops forever never reach here. */
    fprintf(stderr, "Reset returned after %d frames; driving interrupts.\n", frame_count);
    vb_recomp_dump_trace();
    for (;;) vb_interrupt_check();

    vb_platform_shutdown();
    vb_shutdown();
    free(rom);
    return 0;
}
