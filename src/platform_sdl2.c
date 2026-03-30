/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * SDL2 platform backend - window, input, audio.
 */

#include "vbrecomp/platform.h"
#include "vbrecomp/types.h"

#ifdef VBRECOMP_NO_SDL
/* Stub implementation when building without SDL */

bool vb_platform_init(const char *title, int scale) {
    (void)title; (void)scale;
    return false;
}
void vb_platform_present(const uint32_t *framebuffer) { (void)framebuffer; }
bool vb_platform_poll(void) { return false; }
uint16_t vb_platform_get_buttons(void) { return 0; }
void vb_platform_audio_start(vb_audio_callback_t cb) { (void)cb; }
void vb_platform_audio_stop(void) {}
void vb_platform_shutdown(void) {}

#else

#include <SDL2/SDL.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint16_t button_state;
static vb_audio_callback_t audio_cb;
static SDL_AudioDeviceID audio_dev;

bool vb_platform_init(const char *title, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        return false;
    }

    window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VB_SCREEN_WIDTH * scale, VB_SCREEN_HEIGHT * scale,
        SDL_WINDOW_SHOWN
    );
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return false;

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        VB_SCREEN_WIDTH, VB_SCREEN_HEIGHT
    );
    if (!texture) return false;

    button_state = 0;
    audio_cb = NULL;
    audio_dev = 0;

    return true;
}

void vb_platform_present(const uint32_t *framebuffer) {
    SDL_UpdateTexture(texture, NULL, framebuffer, VB_SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static void update_key(SDL_Keycode key, bool pressed) {
    uint16_t bit = 0;
    switch (key) {
    case SDLK_z:      bit = VB_BTN_A; break;
    case SDLK_x:      bit = VB_BTN_B; break;
    case SDLK_a:      bit = VB_BTN_LT; break;
    case SDLK_s:      bit = VB_BTN_RT; break;
    case SDLK_RETURN:  bit = VB_BTN_STA; break;
    case SDLK_RSHIFT:  bit = VB_BTN_SEL; break;
    case SDLK_UP:      bit = VB_BTN_LU; break;
    case SDLK_DOWN:    bit = VB_BTN_LD; break;
    case SDLK_LEFT:    bit = VB_BTN_LL; break;
    case SDLK_RIGHT:   bit = VB_BTN_LR; break;
    case SDLK_i:       bit = VB_BTN_RU; break;
    case SDLK_k:       bit = VB_BTN_RD; break;
    case SDLK_j:       bit = VB_BTN_RL; break;
    case SDLK_l:       bit = VB_BTN_RR; break;
    default: return;
    }
    if (pressed) {
        button_state |= bit;
    } else {
        button_state &= ~bit;
    }
}

bool vb_platform_poll(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_KEYDOWN:
            update_key(e.key.keysym.sym, true);
            break;
        case SDL_KEYUP:
            update_key(e.key.keysym.sym, false);
            break;
        }
    }
    return true;
}

uint16_t vb_platform_get_buttons(void) {
    return button_state;
}

static void sdl_audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    vb_audio_callback_t cb = audio_cb;
    if (cb) {
        int samples = len / (2 * sizeof(int16_t)); /* stereo s16 */
        cb((int16_t *)stream, samples);
    } else {
        memset(stream, 0, len);
    }
}

void vb_platform_audio_start(vb_audio_callback_t callback) {
    audio_cb = callback;
    SDL_AudioSpec want = {0};
    want.freq = 41700; /* VB native sample rate ~41.7kHz */
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = sdl_audio_callback;

    SDL_AudioSpec have;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) {
        SDL_PauseAudioDevice(audio_dev, 0);
    }
}

void vb_platform_audio_stop(void) {
    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }
    audio_cb = NULL;
}

void vb_platform_shutdown(void) {
    vb_platform_audio_stop();
    if (texture) { SDL_DestroyTexture(texture); texture = NULL; }
    if (renderer) { SDL_DestroyRenderer(renderer); renderer = NULL; }
    if (window) { SDL_DestroyWindow(window); window = NULL; }
    SDL_Quit();
}

#endif /* VBRECOMP_NO_SDL */
