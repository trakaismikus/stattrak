/***************************************************************************************************
 * @file    : auto_off_view.c
 * @brief   : Auto Off View source code
 * @author  : Janis Zaharans
 * @date    : Jan 18, 2026
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
view_t g_autoOffView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};


/* > Local Variables ******************************************************************************/
static uint32_t s_startTime = 0;

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  s_startTime = Time_getMillis();
  App_click(800);
}

static bool _eventHandler(uint16_t event) {

  switch(event) {
    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_PRESS_2:
    case EVENT__PRESS_BOTH:
      AppView_set(AppView_getPrevious());
      return true;

    default: break;
  }

  return true;
}


static void _draw(void) {
  char str[16];

  Gfx_drawStringAligned(64, 2, "Auto Power Off in:", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  // Gfx_drawStringScaled(22, 10, "05", &font5x7, 1, 3, 1);
  // Gfx_drawStringScaled(70, 10, "00", &font5x7, 1, 3, 1);
  // Gfx_drawCharScaled(57, 10, ':', &font5x7, 1, 3, 1);

  int32_t countdownMs = (5 * 60 * 1000 + 1000) - ((uint32_t)(Time_getMillis() - s_startTime));
  if(countdownMs < 0)
    countdownMs = 0;
  uint16_t countdown = countdownMs / 1000;
  uint8_t min = countdown / 60;
  uint8_t sec = countdown % 60;
  bool invert = sec == 0 || (min ==0 && sec <= 10);

  if(invert)
    Gfx_fillRoundRect(27, 9, 68, 20, 4, 1);
  else
    Gfx_drawRoundRect(27, 9, 68, 20, 4, 1);

  snprintf(str, sizeof(str), "%02d", min);
  Gfx_drawStringScaled(32, 12, str, &font5x7, !invert, 2, 0);

  snprintf(str, sizeof(str), "%02d", sec);
  Gfx_drawStringScaled(68, 12, str, &font5x7, !invert, 2, 0);

  if(((countdownMs / 500) & 1) == 1)
    Gfx_drawCharScaled(57, 12, ':', &font5x7, !invert, 2, 0);
}


