/***************************************************************************************************
 * @file    : launcher_view.c
 * @brief   : The app menu (design B of the app menu mockups).
 *
 * Both buttons in an app: the app slides up, the menu comes up from below. Each app has a title
 * picture (80 x 24). Button 1 = previous app, button 2 = next app: the pictures slide sideways
 * and the menu wraps around at the ends. Both buttons, or 3 s without a press: the menu slides
 * down and the selected app comes down from above.
 *
 * Screen: title picture at (24, 2), arrows at the left and the right edge, a bar in segments at
 * the bottom (one segment for each app, the selected one is filled).
 *
 * SETTINGS must stay in kApps: it is the way to the firmware update.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "launcher_view.h"

#include "view.h"
#include "animation.h"
#include "app.h"
#include "gfx.h"
#include "font_glyph.h"
#include "app_icons.h"
#include "time.h"
#include "bsp_button.h"

/* > Defines **************************************************************************************/
#define OPEN_MS           260   // App up, menu up from below
#define CLOSE_MS          220   // Menu down, app down from above
#define SLIDE_MS          220   // Title pictures slide sideways
#define SCREEN_W          128
#define PAGE_WIDTH        128   // Distance between two title pictures while they slide

#define COVER_X           24    // Title picture: 80 x 24 at (24, 2)
#define COVER_Y           2
#define COVER_W           80
#define COVER_H           24

#define PAGER_Y           28    // Segment bar: 3 rows from y = 28
#define PAGER_WIDTH       116
#define PAGER_GAP         3

/* > Type definitions *****************************************************************************/
typedef void (*cover_draw_t)(int16_t x, int16_t y);

typedef struct launcher_app_s {
  view_id_t view;
  cover_draw_t drawCover;
} launcher_app_t;

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);

static void _move(int8_t step);
static void _openSelected(void);
static int8_t _indexOf(view_id_t view);
static int8_t _wrap(int16_t index);
static void _drawIcon(int16_t x, int16_t y, const app_icon_t* icon);
static void _drawIconText(int16_t x, int16_t y, const app_icon_t* icon, const char* text);
static void _drawArrows(void);
static void _drawPager(void);

static void _coverKills(int16_t x, int16_t y);
static void _coverKillDead(int16_t x, int16_t y);
static void _coverStats(int16_t x, int16_t y);
static void _coverSettings(int16_t x, int16_t y);

static void _setupSlide(animation_t* animation);
static void _updateSlide(animation_t* animation, uint32_t progress);

/* > Local Variables ******************************************************************************/
view_t g_launcherView = {
  .init = _init,
  .draw = _draw,
  .eventHandler = _eventHandler,
};

// The apps of the menu, in menu order. SETTINGS is the last one and must stay.
static const launcher_app_t kApps[] = {
  { VIEW_ID__TOTAL,     _coverKills    },
  { VIEW_ID__KILL_DEAD, _coverKillDead },
  { VIEW_ID__STATS,     _coverStats    },
  { VIEW_ID__SETTINGS,  _coverSettings },
};
#define APP_COUNT   ((int8_t)(sizeof(kApps) / sizeof(kApps[0])))

static int8_t   s_selected = 0;       // Index in kApps
static int16_t  s_slideX = 0;         // Offset of the selected picture, goes to 0 while it slides
static int16_t  s_slideFrom = 0;
static uint32_t s_lastInputMs = 0;    // The selected app opens LAUNCHER_SELECT_MS after this

static const animation_functions_t kSlideFunc = {
  .setup = _setupSlide,
  .update = _updateSlide,
};

static animation_t s_slide = {
  .func = &kSlideFunc,
  .curve = ANIMATION_CURVE__EASE_OUT,
  .duration = ANIMATION_MS_TO_TICKS(SLIDE_MS),
  .playCount = 1,
};

/* > Function Definitions *************************************************************************/

void Launcher_onBothButtons(void) {
  const view_id_t next = AppView_getNext();

  if(next == VIEW_ID__LAUNCHER) {
    _openSelected();
    return;
  }

  // Open the menu with the current app selected (the first app for a view that is not in the menu)
  const int8_t index = _indexOf(next);
  s_selected = (index >= 0) ? index : 0;
  // Also here and not only in _init(): when the menu was closing, it stays and _init() does not run
  s_lastInputMs = Time_getMillis() + OPEN_MS;
  AppView_setFrom(VIEW_ID__LAUNCHER, APP_VIEW_FROM__BOTTOM, OPEN_MS);
}

void Launcher_update(uint32_t now) {
  if(AppView_get() != VIEW_ID__LAUNCHER || AppView_getNext() != VIEW_ID__LAUNCHER || s_slide.active)
    return;

  // A button that is down stops the 3 s time; it starts again at the release, so the short press
  // event (180 ms after the release) still comes to the menu. The real button state, not the
  // events: a button can be held already when the menu comes back (after a battery warning).
  if(BspButton_getState(0) || BspButton_getState(1)) {
    s_lastInputMs = now;
    return;
  }

  if((int32_t)(now - s_lastInputMs) >= LAUNCHER_SELECT_MS)
    _openSelected();
}

view_id_t Launcher_getSelectedView(void) {
  return kApps[_wrap(s_selected)].view;
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  Animation_stop(&s_slide);
  s_slideX = 0;
  s_selected = _wrap(s_selected);
  // The time starts when the menu is fully in view
  s_lastInputMs = Time_getMillis() + OPEN_MS;
}

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1: _move(-1); return true;
    case EVENT__PRESS_2: _move(+1); return true;
    default: break;
  }
  return false;
}

static void _draw(void) {
  // The selected picture and its two neighbours (they are visible only while the pictures slide)
  for(int8_t i = -1; i <= 1; i++) {
    const int16_t x = COVER_X + s_slideX + i * PAGE_WIDTH;
    if(x >= SCREEN_W || x + COVER_W <= 0)
      continue;
    kApps[_wrap(s_selected + i)].drawCover(x, COVER_Y);
  }

  _drawArrows();
  _drawPager();
}

static void _move(int8_t step) {
  // Start the slide from where the pictures are now, so fast presses do not jump
  const int16_t from = s_slideX + step * PAGE_WIDTH;

  s_selected = _wrap(s_selected + step);
  s_lastInputMs = Time_getMillis() + SLIDE_MS;
  App_click(1000);

  Animation_stop(&s_slide);
  s_slideFrom = (from > PAGE_WIDTH) ? PAGE_WIDTH : (from < -PAGE_WIDTH) ? -PAGE_WIDTH : from;
  s_slideX = s_slideFrom;
  Animation_start(&s_slide);
}

static void _openSelected(void) {
  AppView_setFrom(kApps[_wrap(s_selected)].view, APP_VIEW_FROM__TOP, CLOSE_MS);
}

static int8_t _indexOf(view_id_t view) {
  for(int8_t i = 0; i < APP_COUNT; i++) {
    if(kApps[i].view == view)
      return i;
  }
  return -1;
}

static int8_t _wrap(int16_t index) {
  index %= APP_COUNT;
  if(index < 0)
    index += APP_COUNT;
  return (int8_t)index;
}

// --- Drawing

static void _drawIcon(int16_t x, int16_t y, const app_icon_t* icon) {
  Gfx_drawBitmapRaw(x, y, icon->data, icon->width, icon->height, 1, GFX_ROTATION__0);
}

// Icon and name, side by side, in the middle of the 80 x 24 picture
static void _drawIconText(int16_t x, int16_t y, const app_icon_t* icon, const char* text) {
  const gfx_font_t* font = &kFontNightverbBig;
  const int16_t textWidth = (int16_t)Gfx_getStringWidth(text, font);
  const int16_t gap = (icon->width + 5 + textWidth > 74) ? 3 : 5;
  const int16_t total = icon->width + gap + textWidth;
  const int16_t x0 = x + (COVER_W - total + 1) / 2;

  _drawIcon(x0, y + (COVER_H - icon->height + 1) / 2, icon);
  Gfx_drawString(x0 + icon->width + gap, y + (COVER_H - font->height + 1) / 2, text, font, 1, 0, GFX_ROTATION__0);
}

// Triangles at the left (button 1) and the right (button 2) edge
static void _drawArrows(void) {
  for(int16_t i = 0; i < 7; i++) {
    const int16_t w = 4 - ((i > 3) ? (i - 3) : (3 - i));
    Gfx_drawHLine(6 - w, 9 + i, w, 1);
    Gfx_drawHLine(122, 9 + i, w, 1);
  }
}

// One segment for each app. The filled part moves with the pictures while they slide.
static void _drawPager(void) {
  const int16_t segW = (PAGER_WIDTH - PAGER_GAP * (APP_COUNT - 1)) / APP_COUNT;
  const int16_t step = segW + PAGER_GAP;
  const int16_t total = APP_COUNT * segW + PAGER_GAP * (APP_COUNT - 1);
  const int16_t x0 = (SCREEN_W - total + 1) / 2;

  for(int8_t i = 0; i < APP_COUNT; i++)
    Gfx_drawHLine(x0 + i * step, PAGER_Y + 1, segW, 1);

  // Position in 1/PAGE_WIDTH steps: s_selected while still, between two apps while sliding
  const int32_t pos = (int32_t)s_selected * PAGE_WIDTH - s_slideX + APP_COUNT * PAGE_WIDTH;
  const int8_t a = _wrap(pos / PAGE_WIDTH);
  const int32_t frac = pos % PAGE_WIDTH;
  const int8_t b = _wrap(a + 1);
  int16_t x = x0 + a * step;

  if(frac > 0 && b == a + 1)
    x += (int16_t)((frac * step + PAGE_WIDTH / 2) / PAGE_WIDTH);
  else if(frac > PAGE_WIDTH / 2)
    x = x0 + b * step;   // From the last app to the first one: jump at half way

  Gfx_fillRect(x, PAGER_Y, segW, 3, 1);
}

// --- Title pictures (80 x 24, top left at x, y)

static void _coverKills(int16_t x, int16_t y) {
  _drawIconText(x, y, &kIconCrosshair, "KILLS");
}

static void _coverKillDead(int16_t x, int16_t y) {
  const app_icon_t* cross = &kIconCrosshairSmall;
  const app_icon_t* skull = &kIconSkull;

  _drawIcon(x + 20 - cross->width / 2, y + 12 - cross->height / 2, cross);
  for(int16_t j = 3; j < 21; j += 2)
    Gfx_drawPixel(x + 40, y + j, 1);
  _drawIcon(x + 60 - skull->width / 2, y + 12 - skull->height / 2, skull);
}

static void _coverStats(int16_t x, int16_t y) {
  _drawIconText(x, y, &kIconBars, "STATS");
}

static void _coverSettings(int16_t x, int16_t y) {
  _drawIconText(x, y, &kIconGear, "SETTINGS");
}

// --- Slide animation

static void _setupSlide(animation_t* animation) {
  (void)animation;
  s_slideX = s_slideFrom;
}

static void _updateSlide(animation_t* animation, uint32_t progress) {
  (void)animation;
  s_slideX = (int16_t)ANIM_LERP(s_slideFrom, 0, (int32_t)progress);
}
