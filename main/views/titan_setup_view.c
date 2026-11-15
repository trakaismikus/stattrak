/***************************************************************************************************
 * @file    : titan_setup_view.c
 * @brief   : The settings of the TITAN app (hold button 2 on the TITAN picture in the app menu).
 *
 * The look of the app settings view: rows with a name and a value. Button 1 / 2 = previous /
 * next row. Hold button 1 / 2 = previous / next value (saved at once). On MAG SIZE a long hold
 * goes faster. Row TITAN: hold button 2 = forget the paired Titan (with a question).
 * No radio here. Both buttons go back to the app menu.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "ask_view.h"
#include "titan.h"
#include "gfx.h"
#include "font.h"

/* > Defines **************************************************************************************/
#define SCREEN_W      128
#define LABEL_X       (SCREEN_W / 2 + 3)
#define VALUE_X       (SCREEN_W / 2 + 6)

/* > Type Definitions *****************************************************************************/
typedef enum row_e {
  ROW__MAG_SIZE = 0,
  ROW__AMMO_ALERT,
  ROW__GUN_BATT,
  ROW__TITAN,
  ROW_COUNT,
} row_t;

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _change(int8_t step, uint8_t repeats);
static void _forgetAnswer(bool yes);

/* > Local Variables ******************************************************************************/
view_t g_titanSetupView = {
  .init = _init,
  .eventHandler = _eventHandler,
  .draw = _draw,
};

static const char* const kLabels[ROW_COUNT] = { "MAG SIZE:", "AMMO ALERT:", "GUN BATT:", "TITAN:" };
static const char* const kAmmoAlert[TITAN_AMMO_ALERT_COUNT] = { "OFF", "10%", "20%", "30%" };

static uint8_t s_row = 0;
static uint8_t s_repeats = 0;     // Repeats of the hold that runs now

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  Titan_load();
  s_repeats = 0;
}

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1:
      s_row = (uint8_t)((s_row + ROW_COUNT - 1) % ROW_COUNT);
      App_click(1000);
      return true;

    case EVENT__PRESS_2:
      s_row = (uint8_t)((s_row + 1) % ROW_COUNT);
      App_click(1000);
      return true;

    case EVENT__LONG_PRESS_1:
      s_repeats = 0;
      _change(-1, 0);
      return true;
    case EVENT__LONG_HOLD_1:
      _change(-1, ++s_repeats);
      return true;

    case EVENT__LONG_PRESS_2:
      s_repeats = 0;
      if(s_row == ROW__TITAN) {
        if(Titan_isPaired()) {
          AskView_set("FORGET THE TITAN?", _forgetAnswer);
          AppView_set(VIEW_ID__ASK);
        }
        return true;
      }
      _change(+1, 0);
      return true;
    case EVENT__LONG_HOLD_2:
      if(s_row != ROW__TITAN)
        _change(+1, ++s_repeats);
      return true;

    default: break;
  }
  return false;
}

static void _forgetAnswer(bool yes) {
  if(yes)
    Titan_forget();
  AppView_set(VIEW_ID__TITAN_SETUP);
}

static uint8_t _wrap(int16_t value, uint8_t count) {
  value %= count;
  return (uint8_t)((value < 0) ? value + count : value);
}

static void _change(int8_t step, uint8_t repeats) {
  switch(s_row) {
    case ROW__MAG_SIZE: {
      // After 4 repeats (2.7 s) of a hold: 5 steps at a time (50 rounds)
      const int16_t steps = (int16_t)step * ((repeats >= 4) ? 5 : 1);
      Titan_setMagSize(_wrap((int16_t)Titan_getMagSize() + steps, TITAN_MAG_STEPS + 1));
    } break;
    case ROW__AMMO_ALERT:
      Titan_setAmmoAlert(_wrap((int16_t)Titan_getAmmoAlert() + step, TITAN_AMMO_ALERT_COUNT));
      break;
    case ROW__GUN_BATT:
      Titan_setBattery(_wrap((int16_t)Titan_getBattery() + step, TITAN_BATT_COUNT));
      break;
    default:
      return;
  }
  App_click(1200);
}

static void _valueText(uint8_t row, char* out, size_t size) {
  switch(row) {
    case ROW__MAG_SIZE:
      if(Titan_getMagSize() == 0)
        snprintf(out, size, "OFF");
      else
        snprintf(out, size, "%u", (unsigned)Titan_getCapacity());
      break;
    case ROW__AMMO_ALERT: snprintf(out, size, "%s", kAmmoAlert[Titan_getAmmoAlert() % TITAN_AMMO_ALERT_COUNT]); break;
    case ROW__GUN_BATT:   snprintf(out, size, "%s", Titan_batteryName(Titan_getBattery())); break;
    case ROW__TITAN:      snprintf(out, size, "%s", Titan_isPaired() ? Titan_getSerial() : "NONE"); break;
    default:              out[0] = '\0'; break;
  }
}

static void _drawRow(int16_t y, uint8_t row, bool selected) {
  char value[16];
  _valueText(row, value, sizeof(value));
  if(selected) {
    Gfx_drawStringAligned(LABEL_X, y, kLabels[row], &font5x5, 0, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(VALUE_X, y, value, &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  } else {
    Gfx_drawStringAligned(LABEL_X - 3, y, kLabels[row], &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(VALUE_X, y, value, &font3x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
}

static void _draw(void) {
  _drawRow(2, (uint8_t)((s_row + ROW_COUNT - 1) % ROW_COUNT), false);
  Gfx_fillRect(0, 9, SCREEN_W, 13, 1);
  _drawRow(13, s_row, true);
  if(s_row == ROW__TITAN && Titan_isPaired())
    Gfx_drawStringAligned(SCREEN_W / 2, 24, "HOLD 2: FORGET THIS TITAN", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  else
    _drawRow(24, (uint8_t)((s_row + 1) % ROW_COUNT), false);
}
