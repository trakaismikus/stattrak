/***************************************************************************************************
 * @file    : intro_view.c
 * @brief   : The start picture (docs/DATA.md section 4)
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 *
 * Only when the person set a start picture on the sync page (Launcher_getStartView()). It shows for
 * 2 s after the buttons of the power-on are up, then it slides down and the first app of the menu comes in from above. A press of a button
 * skips the rest of the 2 s. No flash access here: the picture is in RAM (start_pic.c).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "app_view.h"
#include "gfx.h"
#include "launcher_view.h"
#include "start_pic.h"
#include "time.h"
#include "bsp_button.h"

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);
static void _leave(void);

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

static void _leave(void) {
  if(s_reqExit)
    return;
  s_reqExit = true;
  Launcher_openFirstApp();
}

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
      _leave();       // Skip the rest of the 2 s; the press does nothing else
      return true;
    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_1:
    case EVENT__LONG_HOLD_2:
      return true;
    default: break;
  }
  return false;
}

static void _draw(void) {
  const uint8_t* picture = StartPic_get();
  if(picture != NULL)
    Gfx_drawBitmapRaw(0, 0, picture, START_PIC_W, START_PIC_H, 1, GFX_ROTATION__0);

  // The 2 s start when the buttons of the power-on are up (a hold of 10 s opens the update view)
  const uint32_t now = Time_getMillis();
  if(BspButton_getState(0) || BspButton_getState(1))
    s_startTime = now;

  // Not while the picture still slides in (the start of the view), and only one time
  if(!s_reqExit && AppView_get() == VIEW_ID__INTRO && (now - s_startTime) >= START_PIC_SHOW_MS)
    _leave();
}
