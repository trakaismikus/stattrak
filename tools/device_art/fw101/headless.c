/* Headless driver for the 1.0.1 simulator: runs simulator.c's main_loop in virtual time, takes
   a script of button actions on stdin and prints each new display frame with its time.
   Script lines: "wait MS", "click L|R|B", "hold L|R MS", "rec on|off". */
#include <string.h>
#include <stdlib.h>
#define main sim_main
#include "simulator.c"   /* sim/simulator.c of the 1.0.1 source */
#undef main

static uint32_t s_now = 1000;
uint32_t SDL_GetTicks(void) { return s_now; }
void SDL_Delay(uint32_t ms) { s_now += ms; }

#define QN 64
static SDL_Event s_q[QN]; static int s_qh, s_qt;
static void push(uint32_t type, int32_t sym) { s_q[s_qt].type = type; s_q[s_qt].key.keysym.sym = sym; s_qt = (s_qt + 1) % QN; }
int SDL_PollEvent(SDL_Event* e) { if (s_qh == s_qt) return 0; *e = s_q[s_qh]; s_qh = (s_qh + 1) % QN; return 1; }

/* frame capture: the SDL renderer draws 4x4 rects for lit pixels */
static uint8_t s_cur[32][128], s_last[32][128];
static int s_rec = 0, s_dirty = 0;
static const char* s_input = "-";
int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* p) { (void)r; s_cur[p->y / 4][p->x / 4] = 1; return 0; }
void SDL_RenderPresent(SDL_Renderer* r) {
  (void)r;
  if (memcmp(s_cur, s_last, sizeof s_cur) != 0 || s_dirty) {
    memcpy(s_last, s_cur, sizeof s_cur);
    if (s_rec) {
      printf("F %u %s ", s_now, s_input);
      for (int y = 0; y < 32; y++) { for (int x = 0; x < 128; x += 4) printf("%x", s_cur[y][x] << 3 | s_cur[y][x+1] << 2 | s_cur[y][x+2] << 1 | s_cur[y][x+3]); if (y < 31) putchar(','); }
      putchar('\n');
    }
    s_dirty = 0;
  }
  memset(s_cur, 0, sizeof s_cur);
}

static void step(uint32_t ms) { uint32_t end = s_now + ms; while ((int32_t)(end - s_now) > 0) main_loop(); }
static int32_t key(char c) { return c == 'L' ? SDLK_LEFT : SDLK_RIGHT; }

int main(void) {
  BspOled_init(); BspButton_init(); Eeprom_init(&g_eeprom); App_init();
  Animation_setTickCount(s_now);
  AppView_set(VIEW_ID__TOTAL);
  GfxFx_initCollapse(true); GfxFx_reverseCollapse();
  char line[128];
  while (fgets(line, sizeof line, stdin)) {
    char cmd[16] = "", a[16] = ""; unsigned ms = 0;
    sscanf(line, "%15s %15s %u", cmd, a, &ms);
    if (!strcmp(cmd, "wait")) step(atoi(a));
    else if (!strcmp(cmd, "rec")) { s_rec = !strcmp(a, "on"); s_dirty = 1; }
    else if (!strcmp(cmd, "click")) {
      if (a[0] == 'B') { s_input = "B"; push(SDL_KEYDOWN, SDLK_LEFT); push(SDL_KEYDOWN, SDLK_RIGHT); s_dirty = 1; step(90); push(SDL_KEYUP, SDLK_LEFT); push(SDL_KEYUP, SDLK_RIGHT); }
      else { s_input = a[0] == 'L' ? "1" : "2"; push(SDL_KEYDOWN, key(a[0])); s_dirty = 1; step(90); push(SDL_KEYUP, key(a[0])); }
      s_dirty = 1; step(10); s_input = "-"; s_dirty = 1;
    } else if (!strcmp(cmd, "hold")) {      /* SDL key repeat: first repeat after 500 ms, then each 33 ms */
      s_input = a[0] == 'L' ? "H1" : "H2"; push(SDL_KEYDOWN, key(a[0])); s_dirty = 1;
      uint32_t t = 0, next = 500;
      while (t < ms) { step(10); t += 10; if (t >= next) { push(SDL_KEYDOWN, key(a[0])); next += 33; } }
      push(SDL_KEYUP, key(a[0])); s_dirty = 1; step(10); s_input = "-"; s_dirty = 1;
    }
  }
  return 0;
}
