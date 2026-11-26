/***************************************************************************************************
 * @file    : device_model_view.c
 * @brief   : The question "StatTrak version": DIY or PEQ (designs/device-version.json)
 *
 * It shows at a normal start while the device version is not set (a new device, a device after an
 * update from an older firmware, after a factory reset), and after a hold on Settings > Version.
 * Each half of the display is one large button: hold button 1 = DIY, hold button 2 = PEQ. A short
 * press does nothing. App_answerDeviceModel() stores the answer and opens the next screen.
 *
 * The app menu (both buttons) and power off (both buttons, long) work as in an app: the way to
 * Settings and to the firmware update stays open. The question comes again at the next power-on.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "app_view.h"
#include "gfx.h"
#include "font.h"
#include "hold_button.h"

/* > Defines **************************************************************************************/
#define BUTTON_Y        10
#define BUTTON_W        54
#define BUTTON_H        21
#define BUTTON_LEFT_X   3
#define BUTTON_RIGHT_X  71

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

/* > Global Variables ******************************************************************************/
view_t g_deviceModelView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};

/* > Local Variables ******************************************************************************/
static hold_buttons_t s_hold;    // DIY = hold 1, PEQ = hold 2 (main/views/hold_button.h)

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  HoldButtons_reset(&s_hold);
}

static void _answer(uint8_t model) {
  HoldButtons_reset(&s_hold);
  App_click(1200);
  App_answerDeviceModel(model);
}

static bool _eventHandler(uint16_t event) {
  HoldButtons_event(&s_hold, event);

  switch(event) {
    // Only a hold that started on this screen (not the hold that opened it)
    case EVENT__LONG_PRESS_1:
      if(HoldButtons_isHeld(&s_hold, 0))
        _answer(DEVICE_MODEL__DIY);
      return true;

    case EVENT__LONG_PRESS_2:
      if(HoldButtons_isHeld(&s_hold, 1))
        _answer(DEVICE_MODEL__PEQ);
      return true;

    default: break;
  }
  return true;
}

static void _draw(void) {
  Gfx_drawStringAligned(64, 1, "STATTRAK VERSION", &font5x5, 1, 1, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawLine(63, 10, 63, 30, 1);
  Gfx_drawLine(64, 10, 64, 30, 1);

  HoldButton_drawLarge(BUTTON_LEFT_X, BUTTON_Y, BUTTON_W, BUTTON_H, "DIY", HoldButtons_fill(&s_hold, 0));
  HoldButton_drawLarge(BUTTON_RIGHT_X, BUTTON_Y, BUTTON_W, BUTTON_H, "PEQ", HoldButtons_fill(&s_hold, 1));
}
