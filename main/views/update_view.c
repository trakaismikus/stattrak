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
#include <stdio.h>
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
view_t g_updateView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
  .deinit = _deinit,
};


/* > Local Variables ******************************************************************************/
static uint8_t s_cancel = 0;

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
      return true;

    case EVENT__PRESS_2:
      return true;

    case EVENT__LONG_PRESS_1:
      AppView_set(AppView_getPrevious());
      return true;

    case EVENT__LONG_PRESS_2:
      return true;

    case EVENT__RAW_PRESS_1:
      s_cancel = 1;
      break;
    
    case EVENT__RAW_RELEASE_1:
      s_cancel = 0;
      break;

    default: break;
  }

  return false;
}



static void _draw(void) {
  char str[32];
  static const char* kStateStrs[OTA_STATE_COUNT] = {
    [OTA_STATE__IDLE]     = "Waiting",
    [OTA_STATE__STARTED]  = "Initiated",
    [OTA_STATE__WRITING]  = "Receiving",
    [OTA_STATE__FINISHED] = "Completed",
    [OTA_STATE__ACCEPTED] = "Accepted",
    [OTA_STATE__ERROR]    = "Error",
  };

  uint8_t state = Ota_getState();
  if(state >= OTA_STATE_COUNT)
    state = OTA_STATE__ERROR;
  
  // Gfx_drawStringAligned(Gfx_getWidth()/2, 2, "FW UPDATE", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  GUI_DRAW_TITLE((Gfx_getWidth() - (15 * font5x5.x_advance))/2, 1, 15, "FIRMWARE UPDATE");

  if(state == OTA_STATE__IDLE) {
    snprintf(str, sizeof(str), "Waiting%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
    Gfx_drawStringAligned((Gfx_getWidth() - (7 * font5x5.x_advance))/2, 12, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  } else
    Gfx_drawStringAligned(Gfx_getWidth()/2, 12, kStateStrs[state], &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  if(state == OTA_STATE__WRITING) {
    uint8_t progress = Ota_getProgress();
    snprintf(str, sizeof(str), "%d%%", progress);
    Gfx_drawStringAligned(Gfx_getWidth()/2, 23, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    
    Gfx_invertRect(14, 22, progress, 7);
    Gfx_drawRoundRect(13, 21, Gfx_getWidth() - 26, 9, 3, 1);
  } else {
    GUI_DRAW_BTN(12, 20, 42, 11, "CANCEL", s_cancel == 1);
  }

}


