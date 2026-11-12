/***************************************************************************************************
 * @file    : bluetooth_view.c
 * @brief   : Settings > "Bluetooth: ENABLE": pick the firmware update or the app sync.
 *
 * Bluetooth is still off on this screen. It goes on when the next screen opens (the update view or
 * the app sync view call Ota_init()). This screen and the two Bluetooth screens light few pixels:
 * the display current adds to the current peak of the radio start, and on a weak battery path the
 * two together can pull the supply below the brown-out level (docs/BLUETOOTH.md, "Radio start").
 *
 * Hold button 1 = UPDATE (the update view), hold button 2 = APPS (the app sync view). Only a hold
 * that started on this screen counts, not the hold that opened it. A short press of a button = back
 * to Settings (the update view and the app sync view come back here with their BACK / CANCEL).
 **************************************************************************************************/

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "hold_button.h"

static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

#define UPDATE_W  30    // "UPDATE": 6 letters
#define APPS_W    22    // "APPS"

static hold_buttons_t s_hold;

view_t g_bluetoothView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};

static void _init(void) {
  HoldButtons_reset(&s_hold);
}

static bool _eventHandler(uint16_t event) {
  HoldButtons_event(&s_hold, event);

  switch(event) {
    case EVENT__LONG_PRESS_1:
      if(HoldButtons_isHeld(&s_hold, 0)) {
        HoldButtons_reset(&s_hold);   // No filled button while it slides away: the radio starts now
        AppView_set(VIEW_ID__UPDATE);
      }
      return true;

    case EVENT__LONG_PRESS_2:
      if(HoldButtons_isHeld(&s_hold, 1)) {
        HoldButtons_reset(&s_hold);
        AppView_set(VIEW_ID__APP_SYNC);
      }
      return true;

    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
      AppView_set(VIEW_ID__SETTINGS);
      return true;

    default: break;
  }
  return false;
}

static void _draw(void) {
  GUI_DRAW_TITLE_THIN(Gfx_getWidth() / 2, 1, "BLUETOOTH");
  Gfx_drawStringAligned(Gfx_getWidth() / 2, 11, "PRESS = BACK", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  HoldButton_draw(HOLD_BUTTON_MARGIN, UPDATE_W, "UPDATE", HoldButtons_fill(&s_hold, 0));
  HoldButton_draw(HOLD_BUTTON_RIGHT_X(APPS_W), APPS_W, "APPS", HoldButtons_fill(&s_hold, 1));
}
