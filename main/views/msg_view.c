/***************************************************************************************************
 * @file    : msg_view.c
 * @brief   : Message View source code
 * @author  : Janis Zaharans
 * @date    : Aug 31, 2025
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "msg_view.h"
#include "view.h"
#include "app.h"
#include "img.h"
#include "font_glyph.h"

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

/* > Type Definitions *****************************************************************************/

/* > Constants ************************************************************************************/

/* > Local Variables ******************************************************************************/
static const char* s_questionStr = NULL;
static void (*s_answerCallback)(bool) = NULL;
static uint8_t s_answer = 0;

/* > Global Variables *****************************************************************************/
view_t g_msgView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
};

/* > Function Definitions *************************************************************************/

void MsgView_set(const char* msg, void (*answerCallback)(bool)) {
  s_questionStr = msg;
  s_answerCallback = answerCallback;
  s_answer = 0;
}

/* > Local Function Definitions *******************************************************************/

static bool _eventHandler(uint16_t event) {
  
  switch(event) {

    // No
    case EVENT__LONG_PRESS_1:
      if(s_answerCallback)
        s_answerCallback(false);
      return true;

    // Yes
    case EVENT__LONG_PRESS_2:
      if(s_answerCallback)
        s_answerCallback(true);
      return true;

    case EVENT__RAW_PRESS_1: s_answer = 1; break;
    case EVENT__RAW_PRESS_2: s_answer = 2; break;

    case EVENT__RAW_RELEASE_1:
    case EVENT__RAW_RELEASE_2:
      s_answer = 0;
      break;

    default: break;
  }

  return false;
}


static void _draw(void) {
  
  if(s_questionStr) {
    Gfx_drawStringAligned(Gfx_getWidth()/2, 6, (const char*)s_questionStr, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  }
  
  // Gfx_drawBitmap(0, 0, &kBmpWarning, 1, GFX_ROTATION__0);
  // Gfx_drawStringAligned(80, 2, "!!! WARNING !!!", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  // Gfx_drawStringAligned(80, 12, "Battery low!", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  // Gfx_drawStringAligned(80, 20, "Shutdown in 60", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);


  Gfx_fillRoundRect(16, 0, 96, kFontNightverbBig.height+4, 3, 1);
  Gfx_drawStringAligned(Gfx_getWidth()/2, 2, "BATTERY LOW!", &kFontNightverbBig, 0, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  Gfx_drawString(8, 13, "Shutdown in 60 SEC", &font5x5, 1, 0, GFX_ROTATION__0);

  GUI_DRAW_BTN(64-24, 20, 47, 11, "DISMISS", s_answer == 1);

  // GUI_DRAW_BTN(12, 16, 32, 12, "NO", s_answer == 1);
  // GUI_DRAW_BTN(Gfx_getWidth() - 32 - 12, 16, 32, 12, "YES", s_answer == 2);
}