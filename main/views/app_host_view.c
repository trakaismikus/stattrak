/***************************************************************************************************
 * @file    : app_host_view.c
 * @brief   : Runs one installed app with the app engine (docs/APPS.md, section 4).
 *
 * The app can only do what the system calls below let it do. Each value from the app is checked:
 * all drawing is clipped to the screen and has a pixel budget for each frame, the counters change
 * by -10..10 only in a button event and only with the permission, the sound follows the Sound
 * setting. The engine stops an app after 4000 steps for one event.
 *
 * No flash writes in the draw function: an app error and the saved values are written later,
 * in AppHost_update() (App_update).
 *
 * Both buttons are never sent to the app (main.c): short = app menu, long = power off.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_host_view.h"

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "apps.h"
#include "eeprom.h"
#include "battery.h"
#include "gfx.h"
#include "font.h"
#include "font_glyph.h"
#include "time.h"
#include "esp_log.h"

/* > Defines **************************************************************************************/
#define SCREEN_W          128
#define SCREEN_H          32
#define COORD_LIMIT       1024                  // Coordinates from the app are cut to +-1024
#define DRAW_BUDGET       (8 * SCREEN_W * SCREEN_H)   // Pixels for one frame: 8 full screens
#define MAX_DIGITS        10
#define COUNTER_STEP_MAX  10

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);

static void _open(uint32_t id);
static void _close(void);
static void _run(stapp_event_t event);
static uint8_t s_counterChanges = 0;   // add_counter calls in the event that runs now
#define APP_HOST_MAX_COUNTER_CHANGES  4
static void _fail(stapp_result_t result);
static bool _valuesChanged(void);
static void _queueSave(void);
static void _flushPending(void);
static void _drawError(void);

static int32_t _sys(void* ctx, uint8_t sysId, const int32_t* args, bool drawing);
static int32_t _clamp(int32_t value, int32_t low, int32_t high);
static bool _spend(int32_t pixels);
static void _fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color);
static void _rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color);
static void _rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t filled);
static void _pixel(int32_t x, int32_t y, int32_t color);
static void _beginInvert(void);
static void _endInvert(void);
static void _text(int32_t x, int32_t y, const char* str, int32_t font, int32_t color, int32_t align);
static void _bignum(int32_t x, int32_t y, int32_t value, int32_t digits, int32_t scale, int32_t color);
static void _bitmap(int32_t x, int32_t y, int32_t index, int32_t color);
static void _number(char* out, uint32_t size, int32_t value, int32_t digits);
static int32_t _counter(int32_t id);
static int32_t _addCounter(int32_t id, int32_t delta);
static uint32_t _random(void);

/* > Local Variables ******************************************************************************/
static const char* TAG = "AppHost";

view_t g_appHostView = {
  .init = _init,
  .deinit = _deinit,
  .draw = _draw,
  .eventHandler = _eventHandler,
};

static const gfx_font_t* const kFonts[] = { &font5x7, &font5x5, &font3x5, &kFontNightverbBig };
#define FONT_COUNT  ((int32_t)(sizeof(kFonts) / sizeof(kFonts[0])))

// Counter ids of the apps: 0 total kills, 1 total deaths, 2 game kills, 3 game deaths
static const uint32_t kCounterEntries[] = {
  ENTRY_ID__TOTAL_KILLS, ENTRY_ID__TOTAL_DEATHS, ENTRY_ID__GAME_KILLS, ENTRY_ID__GAME_DEATHS,
};

// The app and the engine (static: the main loop has a small stack)
static uint8_t        s_package[STAPP_MAX_SIZE];
static stapp_vm_t     s_vm;
static stapp_info_t   s_info;
static uint32_t       s_nextId = APPS_ID__NONE;   // The app that opens with the next init
static uint32_t       s_id = APPS_ID__NONE;       // The open app
static bool           s_open = false;
static bool           s_loaded = false;           // The package is in RAM and checked
static stapp_result_t s_error = STAPP_OK;
static stapp_event_t  s_event = STAPP_EVENT__OPEN; // The event that runs now
static int32_t        s_budget = 0;
static uint32_t       s_nextTick = 0;
static uint32_t       s_rand = 1;

// Saved variables: the copy in flash, and a save() call that waits for the 30 s limit
static int32_t        s_savedCopy[STAPP_MAX_SAVED];
static bool           s_saveRequested = false;
static uint32_t       s_saveAllowedAt = 0;

// Flash writes for App_update (never while the view draws)
static bool           s_pendingSave = false;
static uint32_t       s_pendingSaveId = APPS_ID__NONE;
static uint8_t        s_pendingSaveCount = 0;
static int32_t        s_pendingValues[STAPP_MAX_SAVED];
static uint32_t       s_pendingErrorId = APPS_ID__NONE;

// Color 2 (invert) of text and digits draws here first
static uint8_t        s_scratchFrame[SCREEN_W * SCREEN_H / 8];
static gfx_ctx_t      s_scratchCtx;
static gfx_ctx_t*     s_realCtx = NULL;

/* > Function Definitions *************************************************************************/

void AppHost_setApp(uint32_t id) {
  if(s_open) {
    if(s_id != id) {
      _close();
      _open(id);
    }
  }
  s_nextId = id;
}

uint32_t AppHost_getAppId(void) {
  return s_open ? s_id : s_nextId;
}

bool AppHost_isOpen(void) {
  return s_open;
}

stapp_result_t AppHost_getError(void) {
  return s_open ? s_error : STAPP_OK;
}

void AppHost_update(uint32_t now) {
  _flushPending();

  if(!s_open || !s_loaded || s_error != STAPP_OK)
    return;

  // Ticks only while the app is the current view (not while it slides in or out)
  if(AppView_get() == VIEW_ID__APP_HOST && AppView_getNext() == VIEW_ID__APP_HOST) {
    if((int32_t)(now - s_nextTick) >= 0) {
      s_nextTick += APP_HOST_TICK_MS;
      if((int32_t)(now - s_nextTick) >= 0)
        s_nextTick = now + APP_HOST_TICK_MS;   // Late: no burst of ticks
      _run(STAPP_EVENT__TICK);
    }
  }

  // save() of the app: at most once in 30 s
  if(s_saveRequested && s_error == STAPP_OK && (int32_t)(now - s_saveAllowedAt) >= 0) {
    s_saveRequested = false;
    if(_valuesChanged()) {
      Apps_saveValues(s_id, s_vm.globals, s_info.savedCount);
      memcpy(s_savedCopy, s_vm.globals, s_info.savedCount * sizeof(int32_t));
      s_saveAllowedAt = now + APPS_SAVE_PERIOD_MS;
    }
  }
}

void AppHost_onShutdown(void) {
  _flushPending();
  if(s_open && s_loaded && s_error == STAPP_OK && _valuesChanged()) {
    Apps_saveValues(s_id, s_vm.globals, s_info.savedCount);
    memcpy(s_savedCopy, s_vm.globals, s_info.savedCount * sizeof(int32_t));
  }
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  if(!s_open)
    _open(s_nextId);
}

static void _deinit(void) {
  _close();
}

static void _draw(void) {
  if(!s_open)
    return;

  if(!s_loaded || s_error != STAPP_OK) {
    _drawError();
    return;
  }

  s_budget = DRAW_BUDGET;
  _run(STAPP_EVENT__DRAW);
}

static bool _eventHandler(uint16_t event) {
  stapp_event_t appEvent;

  switch(event) {
    case EVENT__PRESS_1:      appEvent = STAPP_EVENT__PRESS_1; break;
    case EVENT__PRESS_2:      appEvent = STAPP_EVENT__PRESS_2; break;
    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_HOLD_1:  appEvent = STAPP_EVENT__LONG_1; break;
    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_2:  appEvent = STAPP_EVENT__LONG_2; break;
    // The button goes down: no wait for the release (a format 2 app, docs/APPS.md section 4)
    case EVENT__RAW_PRESS_1:  appEvent = STAPP_EVENT__DOWN_1; break;
    case EVENT__RAW_PRESS_2:  appEvent = STAPP_EVENT__DOWN_2; break;
    default:                  return false;
  }

  // After an app error the buttons do nothing. Both buttons open the app menu (main.c).
  if(s_open && s_loaded && s_error == STAPP_OK)
    _run(appEvent);
  return true;
}

static void _open(uint32_t id) {
  const uint32_t now = Time_getMillis();

  s_id = id;
  s_open = true;
  s_loaded = false;
  s_error = STAPP_OK;
  s_saveRequested = false;
  s_saveAllowedAt = now;
  s_nextTick = now + APP_HOST_TICK_MS;
  s_rand = (now ^ id ^ 0x9E3779B9u) | 1u;
  memset(&s_vm, 0, sizeof(s_vm));

  const stapp_result_t load = Apps_loadPackage(id, s_package, &s_info);
  if(load != STAPP_OK) {
    ESP_LOGE(TAG, "App %08lx: %s", (unsigned long)id, Stapp_resultText(load));
    _fail(load);
    return;
  }

  Stapp_load(&s_vm, s_package, &s_info);
  Apps_loadValues(id, s_vm.globals, s_info.savedCount);
  memcpy(s_savedCopy, s_vm.globals, sizeof(s_savedCopy));
  s_loaded = true;

  _run(STAPP_EVENT__OPEN);
}

static void _close(void) {
  if(!s_open)
    return;

  if(s_loaded && s_error == STAPP_OK) {
    _run(STAPP_EVENT__CLOSE);
    if(s_error == STAPP_OK && _valuesChanged())
      _queueSave();
  }

  s_open = false;
  s_loaded = false;
  s_error = STAPP_OK;
}

static void _run(stapp_event_t event) {
  if(!s_open || !s_loaded || s_error != STAPP_OK)
    return;

  s_event = event;
  s_counterChanges = 0;
  stapp_result_t result = Stapp_run(&s_vm, event, _sys, NULL);
  if(result == STAPP_OK)
    result = s_vm.error;   // A bad argument that a system call found
  if(result != STAPP_OK)
    _fail(result);
}

static void _fail(stapp_result_t result) {
  if(s_error != STAPP_OK)
    return;

  s_error = result;
  s_vm.error = result;
  ESP_LOGW(TAG, "App %08lx stopped: %s (code offset %u)", (unsigned long)s_id, Stapp_resultText(result),
           (unsigned)s_vm.errorPc);

  // The error count goes to flash later (this can be in the draw function)
  if(s_pendingErrorId != APPS_ID__NONE && s_pendingErrorId != s_id)
    Apps_noteError(s_pendingErrorId);
  s_pendingErrorId = s_id;
}

static bool _valuesChanged(void) {
  return s_info.savedCount > 0 &&
         memcmp(s_savedCopy, s_vm.globals, s_info.savedCount * sizeof(int32_t)) != 0;
}

static void _queueSave(void) {
  if(s_pendingSave && s_pendingSaveId != s_id)
    _flushPending();   // An other app waits: write it first (rare: two closes in one loop)

  s_pendingSave = true;
  s_pendingSaveId = s_id;
  s_pendingSaveCount = s_info.savedCount;
  memcpy(s_pendingValues, s_vm.globals, s_info.savedCount * sizeof(int32_t));
  memcpy(s_savedCopy, s_vm.globals, s_info.savedCount * sizeof(int32_t));
}

static void _flushPending(void) {
  if(s_pendingSave) {
    s_pendingSave = false;
    Apps_saveValues(s_pendingSaveId, s_pendingValues, s_pendingSaveCount);
  }
  if(s_pendingErrorId != APPS_ID__NONE) {
    const uint32_t id = s_pendingErrorId;
    s_pendingErrorId = APPS_ID__NONE;
    Apps_noteError(id);
  }
}

static void _drawError(void) {
  const char* text = s_loaded ? Stapp_resultText(s_error) : "cannot load";
  if(!s_loaded && s_error == STAPP_ERR_SIZE)
    text = "missing";

  GUI_DRAW_TITLE((SCREEN_W - 9 * font5x5.x_advance) / 2, 1, 9, "APP ERROR");
  Gfx_drawStringAligned(SCREEN_W / 2, 12, text, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawStringAligned(SCREEN_W / 2, 24, "BOTH BUTTONS: MENU", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
}

// --- System calls (docs/APPS.md, section 4). Each value from the app is checked here.

static int32_t _sys(void* ctx, uint8_t sysId, const int32_t* a, bool drawing) {
  (void)ctx;

  switch(sysId) {
    // Drawing: only in the draw event
    case STAPP_SYS__PIXEL:  if(drawing) _pixel(a[0], a[1], a[2]); return 0;
    case STAPP_SYS__HLINE:  if(drawing) _fill(a[0], a[1], a[2], 1, a[3]); return 0;
    case STAPP_SYS__VLINE:  if(drawing) _fill(a[0], a[1], 1, a[2], a[3]); return 0;
    case STAPP_SYS__RECT:   if(drawing) _rect(a[0], a[1], a[2], a[3], a[4]); return 0;
    case STAPP_SYS__FILL:   if(drawing) _fill(a[0], a[1], a[2], a[3], a[4]); return 0;
    case STAPP_SYS__RRECT:  if(drawing) _rrect(a[0], a[1], a[2], a[3], a[4]); return 0;
    case STAPP_SYS__INVERT: if(drawing) _fill(a[0], a[1], a[2], a[3], 2); return 0;
    case STAPP_SYS__BITMAP: if(drawing) _bitmap(a[0], a[1], a[2], a[3]); return 0;
    case STAPP_SYS__BIGNUM: if(drawing) _bignum(a[0], a[1], a[2], a[3], a[4], a[5]); return 0;

    case STAPP_SYS__TEXT: {
      if(!drawing)
        return 0;
      const char* str = (a[2] >= 0 && a[2] < STAPP_MAX_STRINGS) ? Stapp_string(s_package, &s_info, (uint8_t)a[2]) : NULL;
      if(str == NULL) {
        s_vm.error = STAPP_ERR_ARG;
        return 0;
      }
      _text(a[0], a[1], str, a[3], a[4], a[5]);
    } return 0;

    case STAPP_SYS__NUM: {
      if(!drawing)
        return 0;
      char str[16];
      _number(str, sizeof(str), a[2], a[3]);
      _text(a[0], a[1], str, a[4], a[5], a[6]);
    } return 0;

    case STAPP_SYS__COUNTER:
      return _counter(a[0]);

    case STAPP_SYS__ADD_COUNTER:
      // Permission again (the engine checks it too). Only in a button event: no flash writes
      // in draw, and no app can write the counters each 100 ms.
      if(!(s_info.permissions & STAPP_PERM_COUNTERS))
        return 0;
      if(drawing || !(s_event == STAPP_EVENT__PRESS_1 || s_event == STAPP_EVENT__PRESS_2 ||
                      s_event == STAPP_EVENT__LONG_1 || s_event == STAPP_EVENT__LONG_2))
        return 0;
      // At most 4 add_counter calls for one press (each change is a flash write)
      if(s_counterChanges >= APP_HOST_MAX_COUNTER_CHANGES)
        return _counter(a[0]);
      s_counterChanges++;
      return _addCounter(a[0], a[1]);

    case STAPP_SYS__SETTING:
      return (a[0] >= 0 && a[0] < STAPP_MAX_SETTINGS) ? Apps_getSetting(s_id, (uint8_t)a[0]) : 0;

    case STAPP_SYS__TIME:
      return (int32_t)Time_getMillis();

    case STAPP_SYS__CLICK:
      if((s_info.permissions & STAPP_PERM_SOUND) && !drawing && a[0] >= 200 && a[0] <= 4000)
        App_click((uint16_t)a[0]);   // Follows the Sound setting
      return 0;

    case STAPP_SYS__TONE:
      if((s_info.permissions & STAPP_PERM_SOUND) && !drawing && a[0] >= 200 && a[0] <= 4000 &&
         a[1] >= 10 && a[1] <= 1000)
        App_tone((uint16_t)a[0], (uint16_t)a[1]);   // Follows the Sound setting
      return 0;

    case STAPP_SYS__BATTERY:
      if(Eeprom_read(ENTRY_ID__BATTERY_TYPE) <= BATTERY_TYPE__USB)
        return -1;
      return (int32_t)Battery_readLevel();

    case STAPP_SYS__RANDOM:
      if(a[0] < 1 || a[0] > 65535)
        return 0;
      return (int32_t)(_random() % (uint32_t)a[0]);

    case STAPP_SYS__SAVE:
      s_saveRequested = true;   // Written in AppHost_update(), at most once in 30 s
      return 0;

    case STAPP_SYS__REDRAW:
    default:
      return 0;
  }
}

static int32_t _clamp(int32_t value, int32_t low, int32_t high) {
  return (value < low) ? low : (value > high) ? high : value;
}

// Each frame has a pixel budget. More drawing = app error (the main loop must stay fast).
static bool _spend(int32_t pixels) {
  if(pixels < 1)
    pixels = 1;
  if(pixels > s_budget) {
    s_budget = 0;
    s_vm.error = STAPP_ERR_STEPS;
    return false;
  }
  s_budget -= pixels;
  return true;
}

// A filled rectangle, clipped to the screen. Color 0 off, 1 on, 2 invert.
static void _fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
  if(w <= 0 || h <= 0 || color < 0 || color > 2)
    return;

  x = _clamp(x, -COORD_LIMIT, COORD_LIMIT);
  y = _clamp(y, -COORD_LIMIT, COORD_LIMIT);
  w = _clamp(w, 0, 2 * COORD_LIMIT);
  h = _clamp(h, 0, 2 * COORD_LIMIT);

  const int32_t x0 = _clamp(x, 0, SCREEN_W), x1 = _clamp(x + w, 0, SCREEN_W);
  const int32_t y0 = _clamp(y, 0, SCREEN_H), y1 = _clamp(y + h, 0, SCREEN_H);
  if(x0 >= x1 || y0 >= y1 || !_spend((x1 - x0) * (y1 - y0)))
    return;

  if(color == 2)
    Gfx_invertRect((int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0));
  else
    Gfx_fillRect((int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0), (gfx_color_t)color);
}

// An outline of exactly w x h pixels
static void _rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
  if(w <= 0 || h <= 0)
    return;
  x = _clamp(x, -COORD_LIMIT, COORD_LIMIT);
  y = _clamp(y, -COORD_LIMIT, COORD_LIMIT);
  w = _clamp(w, 1, 2 * COORD_LIMIT);
  h = _clamp(h, 1, 2 * COORD_LIMIT);

  _fill(x, y, w, 1, color);
  if(h > 1)
    _fill(x, y + h - 1, w, 1, color);
  if(h > 2) {
    _fill(x, y + 1, 1, h - 2, color);
    if(w > 1)
      _fill(x + w - 1, y + 1, 1, h - 2, color);
  }
}

// The frame with round corners of the built-in screens (GUI_DRAW_ROUND_RECT)
static void _rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t filled) {
  if(w < 2 || h < 2)
    return;
  x = _clamp(x, -COORD_LIMIT, COORD_LIMIT);
  y = _clamp(y, -COORD_LIMIT, COORD_LIMIT);
  w = _clamp(w, 2, 2 * COORD_LIMIT);
  h = _clamp(h, 2, 2 * COORD_LIMIT);

  _fill(x, y + 1, 1, h - 2, 1);
  _fill(x + w - 1, y + 1, 1, h - 2, 1);
  _fill(x + 1, y, w - 2, 1, 1);
  _fill(x + 1, y + h - 1, w - 2, 1, 1);
  if(filled)
    _fill(x + 1, y + 1, w - 2, h - 2, 1);
}

static void _pixel(int32_t x, int32_t y, int32_t color) {
  if(x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H || color < 0 || color > 2 || !_spend(1))
    return;
  if(color == 2)
    Gfx_invertPixel((int16_t)x, (int16_t)y);
  else
    Gfx_drawPixel((int16_t)x, (int16_t)y, (gfx_color_t)color);
}

// Color 2 for text and digits: draw into a scratch frame, then invert each pixel of it that is on
static void _beginInvert(void) {
  s_realCtx = Gfx_getContext();
  s_scratchCtx = *s_realCtx;
  s_scratchCtx.frame_buf = s_scratchFrame;
  s_scratchCtx.offset.x = 0;
  s_scratchCtx.offset.y = 0;
  memset(s_scratchFrame, 0, sizeof(s_scratchFrame));
  Gfx_setContext(&s_scratchCtx);
}

static void _endInvert(void) {
  Gfx_setContext(s_realCtx);
  for(int32_t i = 0; i < (int32_t)sizeof(s_scratchFrame); i++) {
    const uint8_t bits = s_scratchFrame[i];
    if(bits == 0)
      continue;
    for(int32_t b = 0; b < 8; b++) {
      if(bits & (1 << b))
        Gfx_invertPixel((int16_t)(i % SCREEN_W), (int16_t)((i / SCREEN_W) * 8 + b));
    }
  }
}

// Text in one of the 4 fonts (Gfx_drawStringAligned). Color 2 inverts the pixels of the glyphs.
static void _text(int32_t x, int32_t y, const char* str, int32_t font, int32_t color, int32_t align) {
  if(font < 0 || font >= FONT_COUNT || color < 0 || color > 2 || align < 0 || align > 2)
    return;

  const gfx_font_t* f = kFonts[font];
  const int32_t length = (int32_t)strlen(str);
  if(!_spend(length * (f->height + 2) * 12))
    return;

  static const gfx_align_t kAligns[] = { GFX_ALIGN__LEFT, GFX_ALIGN__CENTER, GFX_ALIGN__RIGHT };
  const int16_t sx = (int16_t)_clamp(x, -COORD_LIMIT, COORD_LIMIT);
  const int16_t sy = (int16_t)_clamp(y, -COORD_LIMIT, COORD_LIMIT);
  if(color == 2) {
    _beginInvert();
    Gfx_drawStringAligned(sx, sy, str, f, 1, 0, kAligns[align], GFX_ROTATION__0);
    _endInvert();
  } else {
    Gfx_drawStringAligned(sx, sy, str, f, (gfx_color_t)color, 0, kAligns[align], GFX_ROTATION__0);
  }
}

// The dotted big digits of KILLS: Gfx_drawStringScaled with font 5x7, scale 2..4
static void _bignum(int32_t x, int32_t y, int32_t value, int32_t digits, int32_t scale, int32_t color) {
  char str[16];

  if(scale < 2 || scale > 4 || color < 0 || color > 2)
    return;
  _number(str, sizeof(str), value, digits);

  const int32_t length = (int32_t)strlen(str);
  if(!_spend(length * 35 * scale * scale))
    return;

  x = _clamp(x, -COORD_LIMIT, COORD_LIMIT);
  y = _clamp(y, -COORD_LIMIT, COORD_LIMIT);
  if(color == 2) {
    _beginInvert();
    Gfx_drawStringScaled((int16_t)x, (int16_t)y, str, &font5x7, 1, (uint8_t)scale, 1);
    _endInvert();
  } else {
    Gfx_drawStringScaled((int16_t)x, (int16_t)y, str, &font5x7, (gfx_color_t)color, (uint8_t)scale, 1);
  }
}

static void _bitmap(int32_t x, int32_t y, int32_t index, int32_t color) {
  uint8_t w = 0, h = 0;
  const uint8_t* data = NULL;

  if(index < 0 || index >= STAPP_MAX_BITMAPS || !Stapp_bitmap(s_package, &s_info, (uint8_t)index, &w, &h, &data)) {
    s_vm.error = STAPP_ERR_ARG;
    return;
  }
  if(color < 0 || color > 2 || !_spend((int32_t)w * h))
    return;

  x = _clamp(x, -COORD_LIMIT, COORD_LIMIT);
  y = _clamp(y, -COORD_LIMIT, COORD_LIMIT);
  if(color != 2) {
    Gfx_drawBitmapRaw((int16_t)x, (int16_t)y, data, w, h, (gfx_color_t)color, GFX_ROTATION__0);
    return;
  }

  for(int32_t px = 0; px < w; px++) {
    for(int32_t py = 0; py < h; py++) {
      const int32_t sx = x + px, sy = y + py;
      if((data[(py / 8) * w + px] & (1 << (py % 8))) && sx >= 0 && sy >= 0 && sx < SCREEN_W && sy < SCREEN_H)
        Gfx_invertPixel((int16_t)sx, (int16_t)sy);
    }
  }
}

// digits 0 = as many as needed, else 0-padded (at most MAX_DIGITS)
static void _number(char* out, uint32_t size, int32_t value, int32_t digits) {
  digits = _clamp(digits, 0, MAX_DIGITS);
  if(digits > 0)
    snprintf(out, size, "%0*ld", (int)digits, (long)value);
  else
    snprintf(out, size, "%ld", (long)value);
}

static int32_t _counter(int32_t id) {
  if(id < 0 || id >= (int32_t)(sizeof(kCounterEntries) / sizeof(kCounterEntries[0])))
    return 0;
  const uint32_t value = Eeprom_read(kCounterEntries[id]);
  return (value > 0x7FFFFFFFu) ? 0x7FFFFFFF : (int32_t)value;
}

// Never below 0. A GAME counter also changes its TOTAL counter, like the built-in screens.
static int32_t _addCounter(int32_t id, int32_t delta) {
  if(id < 0 || id >= (int32_t)(sizeof(kCounterEntries) / sizeof(kCounterEntries[0])))
    return 0;
  if(delta < -COUNTER_STEP_MAX || delta > COUNTER_STEP_MAX)
    return 0;   // Out of range: nothing (docs/APPS.md)
  if(delta == 0)
    return _counter(id);

  for(int32_t pass = 0; pass < 2; pass++) {
    if(pass == 1 && id < 2)
      break;   // A TOTAL counter alone
    const uint32_t entry = (pass == 0) ? kCounterEntries[id] : kCounterEntries[id - 2];

    const uint32_t old = Eeprom_read(entry);
    uint32_t value;
    if(delta < 0)
      value = (old >= (uint32_t)(-delta)) ? old - (uint32_t)(-delta) : 0;
    else
      value = (old <= 0xFFFFFFFFu - (uint32_t)delta) ? old + (uint32_t)delta : 0xFFFFFFFFu;
    Eeprom_write(entry, value);
  }
  return _counter(id);
}

static uint32_t _random(void) {
  // xorshift32
  uint32_t x = s_rand;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s_rand = x;
  return x;
}
