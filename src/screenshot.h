/*
 * screenshot.h — Reusable SDL2 screenshot capture with auto-incrementing filenames.
 *
 * Features:
 *   - Captures the current SDL_Renderer contents to PNG
 *   - Auto-increments: screenshot_0001.png, screenshot_0002.png, ...
 *   - Optional prefix and output directory
 *   - Optional toast callback for UI notification
 *   - Thread-safe counter
 *
 * Dependencies: SDL2, stb_image_write.h
 *
 * Usage:
 *   #include "screenshot.h"
 *
 *   // Basic — saves screenshot_XXXX.png in current directory
 *   screenshot_take(renderer);
 *
 *   // With config
 *   ScreenshotConfig cfg = screenshot_config_default();
 *   cfg.output_dir = "captures";
 *   cfg.prefix = "myapp";           // -> myapp_0001.png
 *   cfg.on_save = my_toast_func;    // called with filename on success
 *   screenshot_take_ex(renderer, &cfg);
 */

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback invoked after a screenshot is saved (e.g. show toast).
 * `filename` is the path that was written. */
typedef void (*screenshot_callback_t)(const char *filename, void *userdata);

typedef struct {
    const char          *output_dir;  /* NULL = current directory */
    const char          *prefix;      /* NULL = "screenshot" */
    screenshot_callback_t on_save;    /* called on success; may be NULL */
    void                *userdata;    /* passed to on_save */
} ScreenshotConfig;

/* Returns a default config: no output dir, "screenshot" prefix, no callback. */
ScreenshotConfig screenshot_config_default(void);

/* Take a screenshot with default settings. Returns true on success. */
bool screenshot_take(SDL_Renderer *renderer);

/* Take a screenshot with custom config. Returns true on success. */
bool screenshot_take_ex(SDL_Renderer *renderer, const ScreenshotConfig *cfg);

/* Take a screenshot with an explicit filename (no counter). */
bool screenshot_take_named(SDL_Renderer *renderer, const char *filepath);

/* Reset the auto-increment counter back to 0. */
void screenshot_reset_counter(void);

/* Get the current counter value (next screenshot number). */
int screenshot_get_counter(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREENSHOT_H */
