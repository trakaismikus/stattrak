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
#include "img.h"
#include "time.h"
#include "animation.h"
#include "gfx_fx.h"

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
view_t g_introView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};


/* > Local Variables ******************************************************************************/
static uint32_t s_startTime = 0;
static bool s_reqExit = false;

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  s_startTime = Time_getMillis();
  s_reqExit = false;
}

static bool _eventHandler(uint16_t event) {

  switch(event) {
    
    case EVENT__PRESS_1: 
      return true;

    case EVENT__PRESS_2:
      return true;

    case EVENT__LONG_PRESS_1:
      return true;

    case EVENT__LONG_PRESS_2:
      return true;

    default: break;
  }

  return false;
}


static void _draw(void) {
  Gfx_drawBitmap(0, 0, &kBmpStattrak, 1, GFX_ROTATION__0);


  if(!s_reqExit) {
    uint32_t tNow = Time_getMillis();
    if((tNow - s_startTime) > 1500) {
      s_reqExit = true;
      AppView_set(VIEW_ID__TOTAL);
    }
  }

  // for(i=0; i<32; i+=2) {
  //   Gfx_drawHLine(0, i, 4 * (i+1), 1);
  // }
  // for(i=0; i<32; i+=2) {
  //   Gfx_drawHLine(0, 31-i, 4 * (i+1), 1);
  // }
}


