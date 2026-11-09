#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "desktop.h"
#include "gfx.h"
#include "gui.h"
#include "input.h"
#include "pit.h"
#include "settings.h"

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t *pixels;
static int width, height;
static mouse_state_t mouse;
static key_event_t queue[64];
static unsigned int qread, qwrite, frames, smoke_frames;

static void enqueue(uint16_t code, char ch) {
    unsigned int next = (qwrite + 1) % 64;
    if (next == qread) qread = (qread + 1) % 64;
    queue[qwrite] = (key_event_t){ code, ch };
    qwrite = next;
}
bool input_pop_key(key_event_t *event) {
    if (qread == qwrite) return false;
    *event = queue[qread];
    qread = (qread + 1) % 64;
    return true;
}
void input_mouse_state(mouse_state_t *out) { *out = mouse; }
void input_set_mouse_position(int x, int y) { mouse.x = x; mouse.y = y; }
uint32_t pit_ticks(void) { return SDL_GetTicks() / 10; }
uint32_t pit_uptime_seconds(void) { return SDL_GetTicks() / 1000; }

static void dispatch_key(SDL_Keycode sym, SDL_Keymod mod) {
    uint16_t code = 0;
    char ch = 0;
    switch (sym) {
        case SDLK_F1: code = KEY_F1; break;
        case SDLK_F2: code = KEY_F2; break;
        case SDLK_F3: code = KEY_F3; break;
        case SDLK_ESCAPE: code = KEY_ESC; break;
        case SDLK_RETURN: code = KEY_ENTER; break;
        case SDLK_BACKSPACE: code = KEY_BACKSPACE; break;
        case SDLK_TAB: code = KEY_TAB; break;
        case SDLK_UP: code = KEY_UP; break;
        case SDLK_DOWN: code = KEY_DOWN; break;
        case SDLK_LEFT: code = KEY_LEFT; break;
        case SDLK_RIGHT: code = KEY_RIGHT; break;
        default:
            if (sym >= 32 && sym <= 126) {
                ch = (char)sym;
                if ((mod & KMOD_SHIFT) && ch >= 'a' && ch <= 'z') ch -= 32;
                code = (uint16_t)(unsigned char)ch;
            }
    }
    if (code) enqueue(code, ch);
}
static void launch_drop(const char *path) {
    if (!path || !*path) return;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("bordify-open", "bordify-open", path, (char *)NULL);
        perror("bordify-open");
        _exit(127);
    }
}
static void save_smoke_frame(void) {
    const char *path = getenv("BORDIFY_SMOKE_BMP");
    if (!path || !*path) return;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height,
                           32, width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s || SDL_SaveBMP(s, path) != 0) {
        fprintf(stderr, "Could not save desktop image: %s\n", SDL_GetError());
        exit(2);
    }
    SDL_FreeSurface(s);
}
void linux_idle(void) {
    SDL_UpdateTexture(texture, NULL, pixels, width * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: exit(0);
            case SDL_MOUSEMOTION: mouse.x = e.motion.x; mouse.y = e.motion.y; break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool down = e.type == SDL_MOUSEBUTTONDOWN;
                mouse.x = e.button.x; mouse.y = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) mouse.left = down;
                if (e.button.button == SDL_BUTTON_MIDDLE) mouse.middle = down;
                if (e.button.button == SDL_BUTTON_RIGHT) mouse.right = down;
                break;
            }
            case SDL_KEYDOWN:
                if (!e.key.repeat) dispatch_key(e.key.keysym.sym, e.key.keysym.mod);
                break;
            case SDL_DROPFILE:
                launch_drop(e.drop.file);
                SDL_free(e.drop.file);
                break;
            default: break;
        }
    }
    if (smoke_frames && ++frames >= smoke_frames) {
        save_smoke_frame();
        exit(0);
    }
    SDL_Delay(16);
}
int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    const char *smoke = getenv("BORDIFY_SMOKE_FRAMES");
    if (smoke && *smoke) smoke_frames = (unsigned int)strtoul(smoke, NULL, 10);
    SDL_DisplayMode dm;
    width = 1024; height = 768;
    if (!smoke_frames && SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        width = dm.w; height = dm.h;
    }
    SDL_Window *window = SDL_CreateWindow("Bordify OS", SDL_WINDOWPOS_CENTERED,
              SDL_WINDOWPOS_CENTERED, width, height,
              smoke_frames ? SDL_WINDOW_SHOWN : SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED);
    if (!window) { fprintf(stderr, "SDL window failed: %s\n", SDL_GetError()); return 1; }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                         SDL_TEXTUREACCESS_STREAMING, width, height) : NULL;
    pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!renderer || !texture || !pixels) {
        fprintf(stderr, "Unable to allocate Bordify framebuffer\n");
        return 1;
    }
    gfx_init((uintptr_t)pixels, (uint32_t)(width * 4), width, height);
    if (!settings_load()) settings_defaults();
    gui_init();
    desktop_run();
    return 0;
}
