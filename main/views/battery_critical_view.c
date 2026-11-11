/***************************************************************************************************
 * @file    : battery_critical_view.c
 * @brief   : Battery Critical View source code
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
view_t g_batteryCriticalView = {
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
  return true;
}

static inline void _drawBattery(int16_t x, int16_t y, bool state) {
  Gfx_drawRoundRect(x, y + 2, 16, 22, 2, 1);
  Gfx_drawRect(x + 1, y + 3, 15, 21, 1);
  Gfx_drawRect(x + 5, y, 7, 3, 1);
  if(state) {
    Gfx_drawVLine(x + 6, y + 8, 5, 1);
    Gfx_drawVLine(x + 7, y + 7, 9, 1);
    Gfx_drawVLine(x + 8, y + 7, 9, 1);
    Gfx_drawVLine(x + 9, y + 8, 5, 1);

    Gfx_drawVLine(x + 7, y + 17, 2, 1);
    Gfx_drawVLine(x + 8, y + 17, 2, 1);
  }
}

static void _draw(void) {
  char str[16];

  int32_t countdownMs = (11 * 1000) - (Time_getMillis() - s_startTime);
  int16_t countdown = countdownMs / 1000;
  if(countdown < 0)
    countdown = 0;

  bool state = (countdownMs / 500) & 1;

  // if(state)
  Gfx_fillRoundRect(32, 0, Gfx_getWidth() - 64, 7, 2, 1);
  Gfx_drawStringAligned(Gfx_getWidth()/2, 1, "WARNING", &font5x5, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  // GUI_DRAW_TITLE((Gfx_getWidth() - (7 * font5x5.x_advance))/2, 1, 7, "WARNING");


  // if(state)
    // Gfx_fillRoundRect((Gfx_getWidth() - 74)/2, 5, 74, 11, 3, 1);
  Gfx_drawStringAligned(64, 11, "Battery", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawStringAligned(64, 20, "Critical!", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  
  snprintf(str, sizeof(str), "%d", countdown);
  Gfx_drawStringScaledAligned(110, 11, str, &font5x5, 1, 2, 0, GFX_ALIGN__CENTER);
  _drawBattery(6, 4, state);
}


