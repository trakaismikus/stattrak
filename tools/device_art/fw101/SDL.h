/* Minimal stand-in for SDL2 (installed as SDL2/SDL.h): virtual time and a scripted key queue, so the
   1.0.1 simulator runs without a window. See make_update_path.py. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#define SDL_INIT_VIDEO 0
#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDLK_ESCAPE 27
#define SDLK_LEFT 0x40000050
#define SDLK_RIGHT 0x4000004F
#define SDL_WINDOWPOS_CENTERED 0
#define SDL_WINDOW_SHOWN 0
typedef struct { int32_t sym; } SDL_Keysym;
typedef struct { SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { uint32_t type; SDL_KeyboardEvent key; } SDL_Event;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
uint32_t SDL_GetTicks(void);
void SDL_Delay(uint32_t ms);
int SDL_PollEvent(SDL_Event* e);
static inline int SDL_Init(uint32_t f) { (void)f; return 0; }
static inline void SDL_Quit(void) {}
static inline SDL_Window* SDL_CreateWindow(const char* t, int x, int y, int w, int h, uint32_t f) { (void)t;(void)x;(void)y;(void)w;(void)h;(void)f; return (SDL_Window*)1; }
static inline SDL_Renderer* SDL_CreateRenderer(SDL_Window* w, int i, uint32_t f) { (void)w;(void)i;(void)f; return (SDL_Renderer*)1; }
static inline SDL_Window* SDL_GetWindowFromID(uint32_t id) { (void)id; return (SDL_Window*)1; }
static inline SDL_Renderer* SDL_GetRenderer(SDL_Window* w) { (void)w; return (SDL_Renderer*)1; }
static inline void SDL_DestroyRenderer(SDL_Renderer* r) { (void)r; }
static inline void SDL_DestroyWindow(SDL_Window* w) { (void)w; }
static inline int SDL_SetRenderDrawColor(SDL_Renderer* r, uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)r;(void)a;(void)b;(void)c;(void)d; return 0; }
static inline int SDL_RenderClear(SDL_Renderer* r) { (void)r; return 0; }
int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* p);
void SDL_RenderPresent(SDL_Renderer* r);
