/*
 * menu.h — Reusable ImGui menu bar system for SDL2 applications.
 *
 * Provides:
 *   - Full ImGui lifecycle (init, shutdown, event handling, draw)
 *   - Customizable dark theme
 *   - Main menu bar with common menus (File, View, Debug, About)
 *   - Toast notification system (bottom-right, auto-fade)
 *   - File open dialog (Windows native, expandable)
 *   - FPS display in menu bar
 *   - About window
 *
 * The menu system is designed to be extended: add your own menus via
 * the callback system, or modify the built-in menus by toggling features.
 *
 * Dependencies: SDL2, Dear ImGui (with SDL2+SDLRenderer2 backends)
 *
 * Usage:
 *   #include "menu.h"
 *
 *   menu_init(window, renderer, NULL);   // NULL = default config
 *   // in event loop:  menu_process_event(&event);
 *   // in draw loop:   menu_draw();
 *   // cleanup:        menu_shutdown();
 */

#ifndef TOOLKIT_MENU_H
#define TOOLKIT_MENU_H

#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ─────────────────────────────────────────────── */

/* Callback for custom menu items. Called inside ImGui::BeginMainMenuBar().
 * Use ImGui calls to add your own menus. */
typedef void (*menu_custom_draw_fn)(void *userdata);

/* File filter for open dialog */
typedef struct {
    const char *description;  /* e.g. "Image Files" */
    const char *extensions;   /* e.g. "*.png;*.jpg;*.bmp" */
} MenuFileFilter;

typedef struct {
    const char       *app_name;        /* shown in About window, window title prefix */
    const char       *app_version;     /* shown in About window */
    const char       *app_description; /* shown in About window */

    /* Which built-in menus to show */
    bool              enable_file_menu;
    bool              enable_view_menu;
    bool              enable_debug_menu;
    bool              enable_about_menu;

    /* File menu options */
    bool              enable_open_file;    /* show "Open..." item */
    const char       *open_dialog_title;   /* dialog title, default "Open File" */
    MenuFileFilter   *file_filters;        /* array of filters, NULL-terminated */
    int               file_filter_count;

    bool              enable_screenshot;   /* show "Screenshot" item (F12) */
    bool              enable_quit;         /* show "Quit" item */

    /* FPS display */
    bool              show_fps_default;    /* start with FPS visible */

    /* Custom menu callback — called after built-in menus, before FPS */
    menu_custom_draw_fn custom_draw;
    void               *custom_userdata;
} MenuConfig;

/* Returns a sensible default config with all common menus enabled. */
MenuConfig menu_config_default(const char *app_name);

/* ── Lifecycle ─────────────────────────────────────────────────── */

void menu_init(SDL_Window *window, SDL_Renderer *renderer, const MenuConfig *cfg);
void menu_shutdown(void);
void menu_process_event(SDL_Event *event);
void menu_draw(void);
bool menu_wants_input(void);

/* ── Actions (polled by your main loop) ────────────────────────── */

bool  menu_screenshot_requested(void);
void  menu_clear_screenshot_request(void);
bool  menu_quit_requested(void);
char *menu_get_opened_file(void);       /* returns path or NULL; caller must free */
void  menu_clear_opened_file(void);

/* ── FPS display ───────────────────────────────────────────────── */

void menu_set_fps(float fps);
void menu_set_status_text(const char *text); /* extra text after FPS, e.g. "| 123K voxels" */
bool menu_get_show_fps(void);

/* ── Toast notification ────────────────────────────────────────── */

/* Show a toast message (bottom-right corner, fades after ~2.5s).
 * Call from anywhere — it's safe from C or C++. */
void menu_show_toast(const char *message);

/* ── Debug console (Windows) ───────────────────────────────────── */

bool menu_get_debug_console(void);

#ifdef __cplusplus
}
#endif

#endif /* TOOLKIT_MENU_H */
