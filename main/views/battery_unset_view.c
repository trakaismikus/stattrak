/***************************************************************************************************
 * @file    : battery_unset_view.c
 * @brief   : The warning "Battery type not set" (designs/battery-not-set.json)
 *
 * Only for the device version DIY. It shows after the answer DIY to the question "StatTrak
 * version", and at each power-on while the battery type is NONE: without a battery type the
 * device shows no level and gives no low battery warning. Hold button 1 = LATER (the start goes
 * on), hold button 2 = SET (Settings opens on the row Battery). The type "USB/DC" is for a device
 * without a battery: with it the warning does not come.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "app_view.h"
#include "gfx.h"
#include "font.h"
#include "hold_button.h"

/* > Defines **************************************************************************************/
#define LATER_W   27    // 5 letters. Odd, like HOLD_BUTTON_W

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

/* > Global Variables ******************************************************************************/
view_t g_batteryUnsetView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};

/* > Local Variables ******************************************************************************/
static hold_buttons_t s_hold;    // LATER = hold 1, SET = hold 2 (main/views/hold_button.h)

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  HoldButtons_reset(&s_hold);
  App_click(800);
}

static bool _eventHandler(uint16_t event) {
  HoldButtons_event(&s_hold, event);

  switch(event) {
    // Only a hold that started on this screen
    case EVENT__LONG_PRESS_1:
      if(HoldButtons_isHeld(&s_hold, 0)) {
        HoldButtons_reset(&s_hold);
        App_closeBatteryUnset(false);
      }
      return true;

    case EVENT__LONG_PRESS_2:
      if(HoldButtons_isHeld(&s_hold, 1)) {
        HoldButtons_reset(&s_hold);
        App_closeBatteryUnset(true);
      }
      return true;

    default: break;
  }
  return true;
}

static void _draw(void) {
  Gfx_fillRoundRect(32, 0, 64, 7, 2, 1);
  Gfx_drawStringAligned(64, 1, "WARNING", &font5x5, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawStringAligned(64, 11, "BATTERY TYPE NOT SET", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  HoldButton_draw(HOLD_BUTTON_MARGIN, LATER_W, "LATER", HoldButtons_fill(&s_hold, 0));
  HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "SET", HoldButtons_fill(&s_hold, 1));
}
