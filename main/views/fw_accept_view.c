/***************************************************************************************************
 * @file    : settings_view.c
 * @brief   : Settings View source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "battery.h"
#include "app_view.h"
#include "board.h"
#include "time.h"

#include "ota.h"
#include "hold_button.h"

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);
static void _deinit(void);

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

/* > Type Definitions *****************************************************************************/

/* > Enumerations *********************************************************************************/

/* > Constants ************************************************************************************/

/* > Local Variables ******************************************************************************/

/* > Global Variables ******************************************************************************/
view_t g_fwAcceptView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
  .deinit = _deinit,
};


/* > Local Variables ******************************************************************************/
static hold_buttons_t s_hold;    // OK: a hold of either button (main/views/hold_button.h)

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  Ota_init();
  HoldButtons_reset(&s_hold);
}

static void _deinit(void) {
  Ota_deinit();
}

// "Complete" is a new screen: a hold that started during "Verifying" does not count
static void _checkScreen(void) {
  static uint8_t s_shownState = 0xFF;
  const uint8_t state = Ota_getState();
  if(state != s_shownState) {
    s_shownState = state;
    HoldButtons_reset(&s_hold);
  }
}

static bool _eventHandler(uint16_t event) {
  _checkScreen();
  HoldButtons_event(&s_hold, event);

  switch(event) {

    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
      return true;

    // OK: a hold of either button, after "Complete"
    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_PRESS_2:
      if(Ota_getState() == OTA_STATE__ACCEPTED &&
         HoldButtons_isHeld(&s_hold, (event == EVENT__LONG_PRESS_1) ? 0 : 1)) {
        AppView_set(VIEW_ID__TOTAL);
      }
      return true;

    default: break;
  }

  return false;
}



static void _draw(void) {
  char str[32];
  _checkScreen();
  uint8_t state = Ota_getState();
  
  // Gfx_drawStringAligned(Gfx_getWidth()/2, 2, "FIRMWARE UPDATE", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  GUI_DRAW_TITLE_THIN(Gfx_getWidth() / 2, 1, "FIRMWARE UPDATE");   // Plain: few lit pixels at the radio start
  

  if(state == OTA_STATE__ACCEPTED) {
    snprintf(str, sizeof(str), "Complete v%s", Board_getFwVersion());
    Gfx_drawStringAligned(Gfx_getWidth()/2, 11, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    HoldButton_draw(HOLD_BUTTON_CENTER_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "OK", HoldButtons_fillAny(&s_hold));
  } else {
    snprintf(str, sizeof(str), "Verifying%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
    Gfx_drawStringAligned((Gfx_getWidth() - (10 * font5x5.x_advance))/2, 16, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }

}


