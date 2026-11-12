/***************************************************************************************************
 * @file    : battery_low_view.c
 * @brief   : Battery Low View source code
 * @author  : Janis Zaharans
 * @date    : Jan 25, 2026
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "battery.h"
#include "app_view.h"
#include "img.h"
#include "time.h"
#include "animation.h"
#include "hold_button.h"

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

/* > Type Definitions *****************************************************************************/

/* > Enumerations *********************************************************************************/

/* > Constants ************************************************************************************/

/* > Local Variables ******************************************************************************/

/* > Global Variables ******************************************************************************/
view_t g_batteryLowView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};


/* > Local Variables ******************************************************************************/
static hold_buttons_t s_hold;    // DISMISS: a hold of either button (main/views/hold_button.h)

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  HoldButtons_reset(&s_hold);
  App_click(800);
}

static bool _eventHandler(uint16_t event) {
  HoldButtons_event(&s_hold, event);

  switch(event) {
    // DISMISS: a hold of either button that started on this screen
    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_PRESS_2:
      if(HoldButtons_isHeld(&s_hold, (event == EVENT__LONG_PRESS_1) ? 0 : 1))
        AppView_set(AppView_getPrevious());
      return true;

    default: break;
  }

  return true;
}

static inline void _drawBattery(int16_t x, int16_t y, bool state) {
  Gfx_drawRoundRect(x, y + 2, 16, 22, 2, 1);
  Gfx_drawRect(x + 1, y + 3, 15, 21, 1);
  Gfx_drawRect(x + 5, y, 7, 3, 1);

  if(state) {
    Gfx_drawHLine(x + 11, y + 17, 2, 1);
    Gfx_drawHLine(x + 8, y + 18, 5, 1);
    Gfx_drawHLine(x + 5, y + 19, 8, 1);
    Gfx_drawHLine(x + 3, y + 20, 10, 1);
  }
}

static void _draw(void) {
  char str[16];
  bool state = (Time_getMillis() >> 9) & 1;

  // if(state)
  Gfx_fillRoundRect(32, 0, Gfx_getWidth() - 64, 7, 2, 1);
  Gfx_drawStringAligned(Gfx_getWidth()/2, 1, "WARNING", &font5x5, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  // GUI_DRAW_TITLE((Gfx_getWidth() - (7 * font5x5.x_advance))/2, 1, 7, "WARNING");


  //   Gfx_fillRoundRect((Gfx_getWidth() - 74)/2, 5, 74, 11, 3, 1);
  Gfx_drawStringAligned(64, 11, "Low Battery", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  snprintf(str, sizeof(str), "(%d%%)", Battery_readLevel());
  Gfx_drawStringAligned(112, 11, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  _drawBattery(6, 4, state);

  HoldButton_draw(HOLD_BUTTON_CENTER_X(34), 34, "DISMISS", HoldButtons_fillAny(&s_hold));
}


