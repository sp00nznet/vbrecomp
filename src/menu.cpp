/*
 * menu.cpp — Reusable ImGui menu bar system for SDL2 applications.
 *
 * Generalized from the 3dSNES menu system. Provides a ready-to-use
 * menu bar with toast notifications, file dialogs, FPS display, and
 * an about window. Extend via the custom_draw callback.
 */

#include "menu.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

/* ── Internal State ────────────────────────────────────────────── */

static struct {
    /* Config (copied at init) */
    char        app_name[128];
    char        app_version[64];
    char        app_description[256];
    bool        enable_file_menu;
    bool        enable_view_menu;
    bool        enable_debug_menu;
    bool        enable_about_menu;
    bool        enable_open_file;
    char        open_dialog_title[128];
    bool        enable_screenshot;
    bool        enable_quit;

    menu_custom_draw_fn custom_draw;
    void               *custom_userdata;

    /* Runtime state */
    bool        screenshot_requested;
    bool        quit_requested;
    char       *opened_file;

    bool        show_fps;
    float       current_fps;
    char        status_text[128];

    bool        show_about;
    bool        show_debug_console;

    /* Toast */
    char        toast_msg[256];
    Uint32      toast_start;
} g_menu;

static SDL_Renderer *g_renderer = NULL;
static SDL_Window   *g_window = NULL;

/* File filter storage */
static MenuFileFilter *g_filters = NULL;
static int             g_filter_count = 0;

/* ── File Dialog ───────────────────────────────────────────────── */

static char *open_file_dialog(void) {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;

    /* Build filter string from config */
    char filter_buf[1024] = {0};
    int pos = 0;
    if (g_filters && g_filter_count > 0) {
        for (int i = 0; i < g_filter_count && pos < 900; i++) {
            int len = snprintf(filter_buf + pos, sizeof(filter_buf) - pos, "%s",
                               g_filters[i].description);
            pos += len + 1;
            len = snprintf(filter_buf + pos, sizeof(filter_buf) - pos, "%s",
                           g_filters[i].extensions);
            pos += len + 1;
        }
        /* "All Files" fallback */
        int len = snprintf(filter_buf + pos, sizeof(filter_buf) - pos, "All Files");
        pos += len + 1;
        snprintf(filter_buf + pos, sizeof(filter_buf) - pos, "*.*");
        pos += 4;
        filter_buf[pos] = '\0'; /* double null terminator */
    } else {
        memcpy(filter_buf, "All Files\0*.*\0", 14);
    }

    ofn.lpstrFilter = filter_buf;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = g_menu.open_dialog_title[0] ? g_menu.open_dialog_title : "Open File";
    if (GetOpenFileNameA(&ofn)) {
        char *result = (char *)malloc(strlen(filename) + 1);
        strcpy(result, filename);
        return result;
    }
#endif
    return NULL;
}

/* ── Theme ─────────────────────────────────────────────────────── */

static void apply_theme(void) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    colors[ImGuiCol_WindowBg]        = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_MenuBarBg]       = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_Header]          = ImVec4(0.15f, 0.30f, 0.55f, 0.80f);
    colors[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.40f, 0.65f, 0.80f);
    colors[ImGuiCol_HeaderActive]    = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_Button]          = ImVec4(0.15f, 0.30f, 0.55f, 0.65f);
    colors[ImGuiCol_ButtonHovered]   = ImVec4(0.20f, 0.40f, 0.65f, 0.80f);
    colors[ImGuiCol_ButtonActive]    = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_CheckMark]       = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]= ImVec4(0.40f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_PopupBg]         = ImVec4(0.08f, 0.08f, 0.12f, 0.96f);
    colors[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]   = ImVec4(0.12f, 0.20f, 0.40f, 1.00f);

    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.GrabRounding   = 3.0f;
    style.PopupRounding  = 4.0f;
}

/* ── About Window ──────────────────────────────────────────────── */

static void draw_about_window(void) {
    if (!g_menu.show_about) return;
    ImGui::SetNextWindowSize(ImVec2(380, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_FirstUseEver);
    char title[160];
    snprintf(title, sizeof(title), "About %s", g_menu.app_name);
    if (ImGui::Begin(title, &g_menu.show_about, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Spacing();
        ImGui::Text("%s  %s", g_menu.app_name, g_menu.app_version);
        ImGui::Separator();
        ImGui::Spacing();
        if (g_menu.app_description[0]) {
            ImGui::TextWrapped("%s", g_menu.app_description);
            ImGui::Spacing();
        }
        ImGui::Separator();
        ImGui::TextDisabled("UI: Dear ImGui + SDL2");
    }
    ImGui::End();
}

/* ── Menu Bar ──────────────────────────────────────────────────── */

static void draw_menu_bar(void) {
    if (!ImGui::BeginMainMenuBar()) return;

    /* ── File ─────────────────────────────────────────────── */
    if (g_menu.enable_file_menu && ImGui::BeginMenu("File")) {
        if (g_menu.enable_open_file) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                char *path = open_file_dialog();
                if (path) {
                    free(g_menu.opened_file);
                    g_menu.opened_file = path;
                }
            }
            ImGui::Separator();
        }
        if (g_menu.enable_screenshot) {
            if (ImGui::MenuItem("Screenshot", "F12")) {
                g_menu.screenshot_requested = true;
            }
            ImGui::Separator();
        }
        if (g_menu.enable_quit) {
            if (ImGui::MenuItem("Quit", "Esc")) {
                g_menu.quit_requested = true;
            }
        }
        ImGui::EndMenu();
    }

    /* ── View ─────────────────────────────────────────────── */
    if (g_menu.enable_view_menu && ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Show FPS", NULL, g_menu.show_fps)) {
            g_menu.show_fps = !g_menu.show_fps;
        }
        ImGui::EndMenu();
    }

    /* ── Debug ────────────────────────────────────────────── */
    if (g_menu.enable_debug_menu && ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Debug Console", NULL, g_menu.show_debug_console)) {
            g_menu.show_debug_console = !g_menu.show_debug_console;
#ifdef _WIN32
            if (g_menu.show_debug_console) {
                AllocConsole();
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);
                char title[160];
                snprintf(title, sizeof(title), "%s Debug Console", g_menu.app_name);
                SetConsoleTitleA(title);
            } else {
                FreeConsole();
            }
#endif
        }
        ImGui::EndMenu();
    }

    /* ── About ────────────────────────────────────────────── */
    if (g_menu.enable_about_menu && ImGui::BeginMenu("About")) {
        char label[160];
        snprintf(label, sizeof(label), "About %s...", g_menu.app_name);
        if (ImGui::MenuItem(label)) {
            g_menu.show_about = true;
        }
        ImGui::EndMenu();
    }

    /* ── Custom menus (user callback) ─────────────────────── */
    if (g_menu.custom_draw) {
        g_menu.custom_draw(g_menu.custom_userdata);
    }

    /* ── FPS display (right-aligned) ──────────────────────── */
    if (g_menu.show_fps) {
        char fps_text[256];
        if (g_menu.status_text[0])
            snprintf(fps_text, sizeof(fps_text), "%.0f FPS %s", g_menu.current_fps, g_menu.status_text);
        else
            snprintf(fps_text, sizeof(fps_text), "%.0f FPS", g_menu.current_fps);
        float text_w = ImGui::CalcTextSize(fps_text).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - text_w - 10);
        ImGui::Text("%s", fps_text);
    }

    ImGui::EndMainMenuBar();
}

/* ── Toast Notification ────────────────────────────────────────── */

static void draw_toast(void) {
    if (g_menu.toast_msg[0] == '\0') return;
    Uint32 elapsed = SDL_GetTicks() - g_menu.toast_start;
    if (elapsed > 2500) {
        g_menu.toast_msg[0] = '\0';
        return;
    }

    float alpha = 1.0f;
    if (elapsed > 2000) alpha = 1.0f - (elapsed - 2000) / 500.0f;

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 pos(io.DisplaySize.x - 20, io.DisplaySize.y - 40);
    ImGui::SetNextWindowPos(pos, 0, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.7f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    if (ImGui::Begin("##toast", NULL,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, alpha));
        ImGui::Text("%s", g_menu.toast_msg);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

/* ── Public API ────────────────────────────────────────────────── */

MenuConfig menu_config_default(const char *app_name) {
    MenuConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.app_name = app_name ? app_name : "Application";
    cfg.app_version = "1.0";
    cfg.app_description = NULL;
    cfg.enable_file_menu = true;
    cfg.enable_view_menu = true;
    cfg.enable_debug_menu = true;
    cfg.enable_about_menu = true;
    cfg.enable_open_file = true;
    cfg.open_dialog_title = "Open File";
    cfg.file_filters = NULL;
    cfg.file_filter_count = 0;
    cfg.enable_screenshot = true;
    cfg.enable_quit = true;
    cfg.show_fps_default = true;
    cfg.custom_draw = NULL;
    cfg.custom_userdata = NULL;
    return cfg;
}

extern "C" void menu_init(SDL_Window *window, SDL_Renderer *renderer, const MenuConfig *cfg) {
    g_window = window;
    g_renderer = renderer;
    memset(&g_menu, 0, sizeof(g_menu));

    /* Apply config */
    MenuConfig c;
    if (cfg) {
        c = *cfg;
    } else {
        c = menu_config_default(NULL);
    }

    snprintf(g_menu.app_name, sizeof(g_menu.app_name), "%s", c.app_name ? c.app_name : "Application");
    snprintf(g_menu.app_version, sizeof(g_menu.app_version), "%s", c.app_version ? c.app_version : "");
    if (c.app_description)
        snprintf(g_menu.app_description, sizeof(g_menu.app_description), "%s", c.app_description);

    g_menu.enable_file_menu = c.enable_file_menu;
    g_menu.enable_view_menu = c.enable_view_menu;
    g_menu.enable_debug_menu = c.enable_debug_menu;
    g_menu.enable_about_menu = c.enable_about_menu;
    g_menu.enable_open_file = c.enable_open_file;
    g_menu.enable_screenshot = c.enable_screenshot;
    g_menu.enable_quit = c.enable_quit;
    g_menu.show_fps = c.show_fps_default;
    g_menu.custom_draw = c.custom_draw;
    g_menu.custom_userdata = c.custom_userdata;

    if (c.open_dialog_title)
        snprintf(g_menu.open_dialog_title, sizeof(g_menu.open_dialog_title), "%s", c.open_dialog_title);

    /* Copy file filters */
    free(g_filters);
    g_filters = NULL;
    g_filter_count = 0;
    if (c.file_filters && c.file_filter_count > 0) {
        g_filter_count = c.file_filter_count;
        g_filters = (MenuFileFilter *)malloc(sizeof(MenuFileFilter) * g_filter_count);
        memcpy(g_filters, c.file_filters, sizeof(MenuFileFilter) * g_filter_count);
    }

    /* Init ImGui */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    apply_theme();
}

extern "C" void menu_shutdown(void) {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    free(g_menu.opened_file);
    g_menu.opened_file = NULL;
    free(g_filters);
    g_filters = NULL;
    g_filter_count = 0;
}

extern "C" void menu_process_event(SDL_Event *event) {
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void menu_draw(void) {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_menu_bar();
    draw_about_window();
    draw_toast();
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

extern "C" bool menu_wants_input(void) {
    ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

extern "C" bool  menu_screenshot_requested(void)     { return g_menu.screenshot_requested; }
extern "C" void  menu_clear_screenshot_request(void)  { g_menu.screenshot_requested = false; }
extern "C" bool  menu_quit_requested(void)            { return g_menu.quit_requested; }
extern "C" char *menu_get_opened_file(void)           { return g_menu.opened_file; }
extern "C" void  menu_clear_opened_file(void)         { free(g_menu.opened_file); g_menu.opened_file = NULL; }
extern "C" void  menu_set_fps(float fps)              { g_menu.current_fps = fps; }
extern "C" bool  menu_get_show_fps(void)              { return g_menu.show_fps; }
extern "C" bool  menu_get_debug_console(void)         { return g_menu.show_debug_console; }

extern "C" void menu_set_status_text(const char *text) {
    if (text)
        snprintf(g_menu.status_text, sizeof(g_menu.status_text), "%s", text);
    else
        g_menu.status_text[0] = '\0';
}

extern "C" void menu_show_toast(const char *msg) {
    snprintf(g_menu.toast_msg, sizeof(g_menu.toast_msg), "%s", msg);
    g_menu.toast_start = SDL_GetTicks();
}
