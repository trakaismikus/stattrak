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
static uint8_t s_answer = 0;

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  Ota_init();
}

static void _deinit(void) {
  Ota_deinit();
}

static bool _eventHandler(uint16_t event) {

  switch(event) {
    
    case EVENT__PRESS_1: 
    case EVENT__PRESS_2:
      if(Ota_getState() == OTA_STATE__ACCEPTED) {
        AppView_set(VIEW_ID__TOTAL);
      }
      return true;

    case EVENT__LONG_PRESS_1:
      return true;

    case EVENT__LONG_PRESS_2:
      return true;

    case EVENT__RAW_PRESS_1:
    case EVENT__RAW_PRESS_2:
      s_answer = 1;
      break;

    case EVENT__RAW_RELEASE_1:
    case EVENT__RAW_RELEASE_2:
      s_answer = 0;
      break;

    default: break;
  }

  return false;
}



static void _draw(void) {
  char str[32];
  uint8_t state = Ota_getState();
  
  // Gfx_drawStringAligned(Gfx_getWidth()/2, 2, "FIRMWARE UPDATE", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  GUI_DRAW_TITLE((Gfx_getWidth() - (15 * font5x5.x_advance))/2, 1, 15, "FIRMWARE UPDATE");
  

  if(state == OTA_STATE__ACCEPTED) {
    snprintf(str, sizeof(str), "Complete v%s", Board_getFwVersion());
    Gfx_drawStringAligned(Gfx_getWidth()/2, 11, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    GUI_DRAW_BTN((Gfx_getWidth() - 31)/2, 20, 31, 11, "OK", s_answer == 1);
  } else {
    snprintf(str, sizeof(str), "Verifying%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
    Gfx_drawStringAligned((Gfx_getWidth() - (10 * font5x5.x_advance))/2, 16, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }

}


