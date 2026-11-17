/***************************************************************************************************
 * @file    : app_host_view.c
 * @brief   : Runs the installed apps with the app engine (docs/APPS.md, section 4).
 *
 * Two engines (runners): FG runs the app that the view shows, BG runs the background app of
 * Settings > Background (docs/APPS.md section 4.6). When the view shows the background app, it
 * shows the BG runner (the app runs once, with one state). The BG runner never draws; it gets the
 * tick each 100 ms and the kill and death events. It stops while Apps: SYNC or the update mode is
 * open, and starts again after them. With popup() (engine 3) it asks to come on the screen: it
 * takes the place of a counter screen or of an other app, never of the menu, the settings, Apps:
 * SYNC, the update mode or a warning (_popup()).
 *
 * The app can only do what the system calls below let it do. Each value from the app is checked:
 * all drawing is clipped to the screen and has a pixel budget for each frame, the counters change
 * by -10..10 only in a button event and only with the permission, the sound follows the Sound
 * setting. The engine stops an app after 4000 steps for one event.
 *
 * No flash writes in the draw function: an app error and the saved values are written later,
 * in AppHost_update() (App_update). The start of the background app after a view closes waits
 * for AppHost_update() too.
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
#include "app_data.h"
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
#define APP_HOST_MAX_COUNTER_CHANGES  4
#define MAX_PENDING_EVENTS            8         // Kill / death events that wait for AppHost_update()

/* > Local Types **********************************************************************************/
typedef struct runner_s {
  uint8_t        package[STAPP_MAX_SIZE];
  stapp_vm_t     vm;
  stapp_info_t   info;
  uint32_t       id;
  bool           open;
  bool           loaded;           // The package is in RAM and checked
  stapp_result_t error;
  stapp_event_t  event;            // The event that runs now
  uint32_t       nextTick;
  uint32_t       rand;
  uint8_t        counterChanges;   // add_counter calls in the event that runs now
  uint8_t        slot;             // The data store (APP_DATA_SLOT_FG / _BG)
  // Saved variables: the copy in flash, and a save() call that waits for the 30 s limit
  int32_t        savedCopy[STAPP_MAX_SAVED];
  bool           saveRequested;
  uint32_t       saveAllowedAt;
} runner_t;

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);

static void _show(uint32_t id);
static void _hide(void);
static void _open(runner_t* r, uint32_t id);
static void _close(runner_t* r);
static bool _isRunning(const runner_t* r);
static void _run(runner_t* r, stapp_event_t event);
static void _tick(runner_t* r, uint32_t now);
static void _saveIfDue(runner_t* r, uint32_t now);
static void _refreshBackground(void);
static bool _onScreen(const runner_t* r);
static void _popup(uint32_t now);
static void _fail(runner_t* r, stapp_result_t result);
static bool _valuesChanged(const runner_t* r);
static void _queueSave(runner_t* r);
static void _flushPending(void);
static void _drawError(const runner_t* r);

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
static int32_t _addCounter(runner_t* r, int32_t id, int32_t delta);
static uint32_t _random(runner_t* r);

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

// The two engines (static: the main loop has a small stack)
static runner_t       s_fg;                      // (no start values: they would put 4 KB into .data)
static runner_t       s_bg;
static runner_t*      s_shown = NULL;            // The runner that the view shows (NULL = none)
static runner_t*      s_cur = NULL;              // The runner whose event runs now
static uint32_t       s_nextId = APPS_ID__NONE;  // The app that the view shows with the next init
static int32_t        s_budget = 0;

// The background app: stopped while Apps: SYNC / the update mode is open; a start that waits for
// AppHost_update(); kill and death events of the counters that wait for AppHost_update()
static bool           s_bgPaused = false;
static bool           s_bgRefreshLater = false;
static uint8_t        s_pendingKills = 0;
static uint8_t        s_pendingDeaths = 0;
static bool           s_popupRequested = false;   // The background app called popup(): AppHost_update()

// Flash writes for App_update (never while the view draws)
static bool           s_pendingSave = false;
static uint32_t       s_pendingSaveId = APPS_ID__NONE;
static uint8_t        s_pendingSaveCount = 0;
static int32_t        s_pendingValues[STAPP_MAX_SAVED];
static uint32_t       s_pendingErrorIds[2] = { APPS_ID__NONE, APPS_ID__NONE };   // One for each runner

// Color 2 (invert) of text and digits draws here first
static uint8_t        s_scratchFrame[SCREEN_W * SCREEN_H / 8];
static gfx_ctx_t      s_scratchCtx;
static gfx_ctx_t*     s_realCtx = NULL;

/* > Function Definitions *************************************************************************/

void AppHost_setApp(uint32_t id) {
  if(s_shown != NULL && s_shown->id != id) {
    _hide();
    _show(id);
  }
  s_nextId = id;
}

uint32_t AppHost_getAppId(void) {
  return (s_shown != NULL) ? s_shown->id : s_nextId;
}

bool AppHost_isOpen(void) {
  return s_shown != NULL;
}

stapp_result_t AppHost_getError(void) {
  return (s_shown != NULL) ? s_shown->error : STAPP_OK;
}

uint32_t AppHost_getBackgroundId(void) {
  return s_bg.open ? s_bg.id : APPS_ID__NONE;
}

stapp_result_t AppHost_getBackgroundError(void) {
  return s_bg.open ? s_bg.error : STAPP_OK;
}

void AppHost_update(uint32_t now) {
  _flushPending();
  if(s_bgRefreshLater) {
    s_bgRefreshLater = false;
    _refreshBackground();
  }
  AppData_update(now);

  // The background app: the kill and death events, then the tick (also while the view shows it)
  if(_isRunning(&s_bg)) {
    while(s_pendingKills > 0 && _isRunning(&s_bg)) {
      s_pendingKills--;
      _run(&s_bg, STAPP_EVENT__KILL);
    }
    while(s_pendingDeaths > 0 && _isRunning(&s_bg)) {
      s_pendingDeaths--;
      _run(&s_bg, STAPP_EVENT__DEATH);
    }
    _tick(&s_bg, now);
  }
  s_pendingKills = 0;
  s_pendingDeaths = 0;
  if(s_popupRequested) {
    s_popupRequested = false;
    _popup(now);
  }

  // The app of the view ticks only while it is the current view (not while it slides in or out)
  if(s_shown == &s_fg && _isRunning(&s_fg) &&
     AppView_get() == VIEW_ID__APP_HOST && AppView_getNext() == VIEW_ID__APP_HOST)
    _tick(&s_fg, now);

  _saveIfDue(&s_fg, now);
  _saveIfDue(&s_bg, now);
}

bool AppHost_keepsAwake(void) {
  return s_shown != NULL && _isRunning(s_shown) && (s_shown->info.permissions & STAPP_PERM_AWAKE) &&
         AppView_get() == VIEW_ID__APP_HOST && AppView_getNext() == VIEW_ID__APP_HOST;
}

void AppHost_onShutdown(void) {
  _flushPending();
  AppData_flush();
  runner_t* const runners[] = { &s_fg, &s_bg };
  for(uint8_t i = 0; i < 2; i++) {
    runner_t* r = runners[i];
    if(_isRunning(r) && _valuesChanged(r)) {
      Apps_saveValues(r->id, r->vm.globals, r->info.savedCount);
      memcpy(r->savedCopy, r->vm.globals, r->info.savedCount * sizeof(int32_t));
    }
  }
}

void AppHost_counterAdded(uint8_t counter) {
  if(!_isRunning(&s_bg))
    return;
  // An add_counter() of the background app itself is no event for it
  if(s_cur == &s_bg)
    return;
  if(counter == APP_HOST_COUNTER__KILLS && s_pendingKills < MAX_PENDING_EVENTS)
    s_pendingKills++;
  else if(counter == APP_HOST_COUNTER__DEATHS && s_pendingDeaths < MAX_PENDING_EVENTS)
    s_pendingDeaths++;
}

void AppHost_backgroundRefresh(void) {
  s_bgRefreshLater = false;
  _refreshBackground();
}

void AppHost_backgroundPause(bool paused) {
  s_bgPaused = paused;
  if(paused)
    _refreshBackground();       // Now: the view of Apps: SYNC opens from a button event
  else
    s_bgRefreshLater = true;    // The view closes (this can be in a draw): AppHost_update() starts it
}

void AppHost_backgroundForget(void) {
  if(s_shown == &s_bg)
    s_shown = NULL;
  s_bg.open = false;
  s_bg.loaded = false;
  s_bg.error = STAPP_OK;
  s_bgRefreshLater = false;
  s_pendingKills = 0;
  s_pendingDeaths = 0;
  s_popupRequested = false;
  // The flash is erased: nothing that waits is written
  s_pendingSave = false;
  s_pendingErrorIds[0] = APPS_ID__NONE;
  s_pendingErrorIds[1] = APPS_ID__NONE;
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  if(s_shown == NULL)
    _show(s_nextId);
}

static void _deinit(void) {
  _hide();
}

static void _draw(void) {
  runner_t* r = s_shown;
  if(r == NULL)
    return;

  if(!r->loaded || r->error != STAPP_OK) {
    _drawError(r);
    return;
  }

  s_budget = DRAW_BUDGET;
  _run(r, STAPP_EVENT__DRAW);
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
  if(s_shown != NULL && _isRunning(s_shown))
    _run(s_shown, appEvent);
  return true;
}

// The view shows `id`: the background runner if it runs that app, else the FG runner opens it
static void _show(uint32_t id) {
  if(s_bg.open && s_bg.id == id) {
    s_shown = &s_bg;
    return;
  }
  _open(&s_fg, id);
  s_shown = &s_fg;
}

// The view closes or shows an other app. The background app runs on.
static void _hide(void) {
  if(s_shown == &s_fg) {
    const uint32_t id = s_fg.id;
    _close(&s_fg);
    // The background app waited for this app to close (it was open here when the setting chose it)
    if(id == Apps_getBackground() && !s_bg.open)
      s_bgRefreshLater = true;
  }
  s_shown = NULL;
}

static void _open(runner_t* r, uint32_t id) {
  const uint32_t now = Time_getMillis();

  r->id = id;
  r->slot = (r == &s_bg) ? APP_DATA_SLOT_BG : APP_DATA_SLOT_FG;
  r->open = true;
  r->loaded = false;
  r->error = STAPP_OK;
  r->saveRequested = false;
  r->saveAllowedAt = now;
  r->nextTick = now + APP_HOST_TICK_MS;
  r->rand = (now ^ id ^ (r == &s_bg ? 0x7F4A7C15u : 0x9E3779B9u)) | 1u;
  memset(&r->vm, 0, sizeof(r->vm));

  const stapp_result_t load = Apps_loadPackage(id, r->package, &r->info);
  if(load != STAPP_OK) {
    ESP_LOGE(TAG, "App %08lx: %s", (unsigned long)id, Stapp_resultText(load));
    _fail(r, load);
    return;
  }

  Stapp_load(&r->vm, r->package, &r->info);
  Apps_loadValues(id, r->vm.globals, r->info.savedCount);
  AppData_open(r->slot, id, (r->info.permissions & STAPP_PERM_DATA) ? r->info.columnCount : 0);
  memcpy(r->savedCopy, r->vm.globals, sizeof(r->savedCopy));
  r->loaded = true;

  _run(r, STAPP_EVENT__OPEN);
}

static void _close(runner_t* r) {
  if(!r->open)
    return;

  if(r->loaded && r->error == STAPP_OK) {
    _run(r, STAPP_EVENT__CLOSE);
    if(r->error == STAPP_OK && _valuesChanged(r))
      _queueSave(r);
  }
  // The rows of the app: written by AppData_update() in the next loop (never here: this can be a draw)
  AppData_closeLater(r->slot);

  r->open = false;
  r->loaded = false;
  r->error = STAPP_OK;
}

static bool _isRunning(const runner_t* r) {
  return r->open && r->loaded && r->error == STAPP_OK;
}

// The runner is the app that the screen shows now (not while a view slides in or out)
static bool _onScreen(const runner_t* r) {
  return r == s_shown && AppView_get() == VIEW_ID__APP_HOST && AppView_getNext() == VIEW_ID__APP_HOST;
}

// popup() of the background app (docs/APPS.md section 4.6): the app comes on the screen in the place
// of a counter screen or of an other app, and the screen gets bright (an activity, like a button).
// Never over the app menu, the settings, Apps: SYNC, the update mode, a question or a warning: the
// app calls popup() again in a later tick.
static void _popup(uint32_t now) {
  if(!_isRunning(&s_bg))
    return;
  const view_id_t view = AppView_get();
  if(view != AppView_getNext())
    return;                       // A view slides in or out
  if(view == VIEW_ID__APP_HOST) {
    if(s_shown != &s_bg) {
      AppHost_setApp(s_bg.id);    // An other app closes, the background app takes its place
      if(s_shown == NULL)
        _show(s_bg.id);
    }
  } else if(view == VIEW_ID__TOTAL || view == VIEW_ID__KILL_DEAD || view == VIEW_ID__STATS) {
    AppHost_setApp(s_bg.id);
    AppView_set(VIEW_ID__APP_HOST);
  } else {
    return;
  }
  App_updateActivity(now);
}

// Settings > Background, Apps: SYNC and the update mode: the BG runner runs the app it must run now
static void _refreshBackground(void) {
  uint32_t wanted = s_bgPaused ? APPS_ID__NONE : Apps_getBackground();
  // The view shows this app in the FG runner now: the background start waits for its close (_hide)
  if(wanted != APPS_ID__NONE && s_fg.open && s_fg.id == wanted)
    wanted = APPS_ID__NONE;

  if(s_bg.open && s_bg.id == wanted)
    return;
  if(s_bg.open) {
    if(s_shown == &s_bg)
      s_shown = NULL;     // Not in use: only views other than the app host stop the background app
    _close(&s_bg);
  }
  s_pendingKills = 0;
  s_pendingDeaths = 0;
  if(wanted != APPS_ID__NONE)
    _open(&s_bg, wanted);
}

static void _run(runner_t* r, stapp_event_t event) {
  if(!_isRunning(r))
    return;

  runner_t* const before = s_cur;
  s_cur = r;
  r->event = event;
  r->counterChanges = 0;
  stapp_result_t result = Stapp_run(&r->vm, event, _sys, r);
  if(result == STAPP_OK)
    result = r->vm.error;   // A bad argument that a system call found
  if(result != STAPP_OK)
    _fail(r, result);
  s_cur = before;
}

static void _tick(runner_t* r, uint32_t now) {
  if((int32_t)(now - r->nextTick) >= 0) {
    r->nextTick += APP_HOST_TICK_MS;
    if((int32_t)(now - r->nextTick) >= 0)
      r->nextTick = now + APP_HOST_TICK_MS;   // Late: no burst of ticks
    _run(r, STAPP_EVENT__TICK);
  }
}

// save() of the app: at most once in 30 s
static void _saveIfDue(runner_t* r, uint32_t now) {
  if(!_isRunning(r) || !r->saveRequested || (int32_t)(now - r->saveAllowedAt) < 0)
    return;
  r->saveRequested = false;
  if(_valuesChanged(r)) {
    Apps_saveValues(r->id, r->vm.globals, r->info.savedCount);
    memcpy(r->savedCopy, r->vm.globals, r->info.savedCount * sizeof(int32_t));
    r->saveAllowedAt = now + APPS_SAVE_PERIOD_MS;
  }
}

static void _fail(runner_t* r, stapp_result_t result) {
  if(r->error != STAPP_OK)
    return;

  r->error = result;
  r->vm.error = result;
  ESP_LOGW(TAG, "App %08lx stopped: %s (code offset %u)", (unsigned long)r->id, Stapp_resultText(result),
           (unsigned)r->vm.errorPc);

  // The error count goes to flash later, in AppHost_update() (this can be in the draw function).
  // A free place, or the same app: one error count for each app and loop.
  for(uint8_t i = 0; i < 2; i++) {
    if(s_pendingErrorIds[i] == r->id)
      return;
  }
  for(uint8_t i = 0; i < 2; i++) {
    if(s_pendingErrorIds[i] == APPS_ID__NONE) {
      s_pendingErrorIds[i] = r->id;
      return;
    }
  }
  ESP_LOGW(TAG, "App %08lx: the error count waits for no place", (unsigned long)r->id);
}

static bool _valuesChanged(const runner_t* r) {
  return r->info.savedCount > 0 &&
         memcmp(r->savedCopy, r->vm.globals, r->info.savedCount * sizeof(int32_t)) != 0;
}

static void _queueSave(runner_t* r) {
  if(s_pendingSave && s_pendingSaveId != r->id)
    _flushPending();   // An other app waits: write it first (rare: two closes in one loop)

  s_pendingSave = true;
  s_pendingSaveId = r->id;
  s_pendingSaveCount = r->info.savedCount;
  memcpy(s_pendingValues, r->vm.globals, r->info.savedCount * sizeof(int32_t));
  memcpy(r->savedCopy, r->vm.globals, r->info.savedCount * sizeof(int32_t));
}

static void _flushPending(void) {
  if(s_pendingSave) {
    s_pendingSave = false;
    Apps_saveValues(s_pendingSaveId, s_pendingValues, s_pendingSaveCount);
  }
  for(uint8_t i = 0; i < 2; i++) {
    if(s_pendingErrorIds[i] != APPS_ID__NONE) {
      const uint32_t id = s_pendingErrorIds[i];
      s_pendingErrorIds[i] = APPS_ID__NONE;
      Apps_noteError(id);
    }
  }
}

static void _drawError(const runner_t* r) {
  const char* text = r->loaded ? Stapp_resultText(r->error) : "cannot load";
  if(!r->loaded && r->error == STAPP_ERR_SIZE)
    text = "missing";

  GUI_DRAW_TITLE((SCREEN_W - 9 * font5x5.x_advance) / 2, 1, 9, "APP ERROR");
  Gfx_drawStringAligned(SCREEN_W / 2, 12, text, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawStringAligned(SCREEN_W / 2, 24, "BOTH BUTTONS: MENU", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
}

// --- System calls (docs/APPS.md, section 4). Each value from the app is checked here.

static int32_t _sys(void* ctx, uint8_t sysId, const int32_t* a, bool drawing) {
  runner_t* const r = (runner_t*)ctx;
  const uint16_t permissions = r->info.permissions;

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
      const char* str = (a[2] >= 0 && a[2] < STAPP_MAX_STRINGS) ? Stapp_string(r->package, &r->info, (uint8_t)a[2]) : NULL;
      if(str == NULL) {
        r->vm.error = STAPP_ERR_ARG;
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
      if(!(permissions & STAPP_PERM_COUNTERS))
        return 0;
      if(drawing || !(r->event == STAPP_EVENT__PRESS_1 || r->event == STAPP_EVENT__PRESS_2 ||
                      r->event == STAPP_EVENT__LONG_1 || r->event == STAPP_EVENT__LONG_2))
        return 0;
      // At most 4 add_counter calls for one press (each change is a flash write)
      if(r->counterChanges >= APP_HOST_MAX_COUNTER_CHANGES)
        return _counter(a[0]);
      r->counterChanges++;
      return _addCounter(r, a[0], a[1]);

    case STAPP_SYS__SETTING:
      return (a[0] >= 0 && a[0] < STAPP_MAX_SETTINGS) ? Apps_getSetting(r->id, (uint8_t)a[0]) : 0;

    case STAPP_SYS__TIME:
      return (int32_t)Time_getMillis();

    case STAPP_SYS__CLICK:
      if((permissions & STAPP_PERM_SOUND) && !drawing && a[0] >= 200 && a[0] <= 4000)
        App_click((uint16_t)a[0]);   // Follows the Sound setting
      return 0;

    case STAPP_SYS__TONE:
      if((permissions & STAPP_PERM_SOUND) && !drawing && a[0] >= 200 && a[0] <= 4000 &&
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
      return (int32_t)(_random(r) % (uint32_t)a[0]);

    case STAPP_SYS__SAVE:
      r->saveRequested = true;   // Written in AppHost_update(), at most once in 30 s
      return 0;

    // The data store of the app (docs/DATA.md section 2). Rows go to flash later, in AppData_update().
    case STAPP_SYS__LOG:
      if(!(permissions & STAPP_PERM_DATA) || drawing)
        return 0;
      return AppData_log(r->slot, a) ? 1 : 0;

    case STAPP_SYS__LOG_COUNT:
      return (permissions & STAPP_PERM_DATA) ? (int32_t)AppData_rows(r->slot) : 0;

    case STAPP_SYS__LOG_GET:
      return (permissions & STAPP_PERM_DATA) ? AppData_get(r->slot, a[0], a[1]) : 0;

    case STAPP_SYS__LOG_CLEAR:
      if(!(permissions & STAPP_PERM_DATA) || drawing)
        return 0;
      AppData_clear(r->slot);
      return 1;

    case STAPP_SYS__BATTERY_MV:
      if(Eeprom_read(ENTRY_ID__BATTERY_TYPE) <= BATTERY_TYPE__USB)
        return -1;
      return (int32_t)Battery_readVoltage();

    case STAPP_SYS__BACKGROUND:
      return (r == &s_bg) ? 1 : 0;

    // Engine 3: the background app asks to come on the screen; AppHost_update() does it (_popup()).
    // The app on the screen: only the activity (the screen gets bright). Gives 1 if it is on the screen.
    case STAPP_SYS__POPUP:
      if(!(permissions & STAPP_PERM_BACKGROUND) || drawing)
        return _onScreen(r) ? 1 : 0;
      if(r == &s_bg)
        s_popupRequested = true;
      else if(_onScreen(r))
        App_updateActivity(Time_getMillis());
      return _onScreen(r) ? 1 : 0;

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
    if(s_cur != NULL)
      s_cur->vm.error = STAPP_ERR_STEPS;
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

  if(s_cur == NULL)
    return;
  if(index < 0 || index >= STAPP_MAX_BITMAPS || !Stapp_bitmap(s_cur->package, &s_cur->info, (uint8_t)index, &w, &h, &data)) {
    s_cur->vm.error = STAPP_ERR_ARG;
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
// More game kills or deaths: an event for the background app (not for the app that adds them).
static int32_t _addCounter(runner_t* r, int32_t id, int32_t delta) {
  (void)r;   // s_cur is r: AppHost_counterAdded() knows the app that adds
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
  if(delta > 0 && id == 2)
    AppHost_counterAdded(APP_HOST_COUNTER__KILLS);
  else if(delta > 0 && id == 3)
    AppHost_counterAdded(APP_HOST_COUNTER__DEATHS);
  return _counter(id);
}

static uint32_t _random(runner_t* r) {
  // xorshift32
  uint32_t x = r->rand;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  r->rand = x;
  return x;
}
