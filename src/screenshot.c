/*
 * screenshot.c — SDL2 screenshot capture implementation.
 */

#include "screenshot.h"
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_shot_counter = 0;

ScreenshotConfig screenshot_config_default(void) {
    ScreenshotConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    return cfg;
}

static bool capture_and_save(SDL_Renderer *renderer, const char *filepath) {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);

    uint8_t *pixels = (uint8_t *)malloc(w * h * 4);
    if (!pixels) return false;

    bool ok = false;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32, pixels, w * 4) == 0) {
        if (stbi_write_png(filepath, w, h, 4, pixels, w * 4)) {
            printf("Screenshot saved: %s\n", filepath);
            fflush(stdout);
            ok = true;
        } else {
            printf("Screenshot FAILED: %s\n", filepath);
            fflush(stdout);
        }
    }
    free(pixels);
    return ok;
}

bool screenshot_take(SDL_Renderer *renderer) {
    return screenshot_take_ex(renderer, NULL);
}

bool screenshot_take_ex(SDL_Renderer *renderer, const ScreenshotConfig *cfg) {
    const char *prefix = "screenshot";
    const char *dir = NULL;

    if (cfg) {
        if (cfg->prefix) prefix = cfg->prefix;
        dir = cfg->output_dir;
    }

    char filename[1024];
    if (dir && dir[0]) {
        snprintf(filename, sizeof(filename), "%s/%s_%04d.png", dir, prefix, g_shot_counter++);
    } else {
        snprintf(filename, sizeof(filename), "%s_%04d.png", prefix, g_shot_counter++);
    }

    bool ok = capture_and_save(renderer, filename);

    if (ok && cfg && cfg->on_save) {
        cfg->on_save(filename, cfg->userdata);
    }

    return ok;
}

bool screenshot_take_named(SDL_Renderer *renderer, const char *filepath) {
    return capture_and_save(renderer, filepath);
}

void screenshot_reset_counter(void) {
    g_shot_counter = 0;
}

int screenshot_get_counter(void) {
    return g_shot_counter;
}
