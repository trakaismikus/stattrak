/***************************************************************************************************
 * @file    : ask_view.c
 * @brief   : Ask View source code
 * @author  : Janis Zaharans
 * @date    : Jun 18, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "ask_view.h"
#include "view.h"
#include "app.h"
#include "hold_button.h"

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
static hold_buttons_t s_hold;    // NO = hold 1, YES = hold 2 (main/views/hold_button.h)

/* > Global Variables *****************************************************************************/
view_t g_askView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
};

/* > Function Definitions *************************************************************************/

void AskView_set(const char* question, void (*answerCallback)(bool)) {
  s_questionStr = question;
  s_answerCallback = answerCallback;
  HoldButtons_reset(&s_hold);
}

/* > Local Function Definitions *******************************************************************/

static bool _eventHandler(uint16_t event) {
  HoldButtons_event(&s_hold, event);

  switch(event) {

    // No: only a hold that started on this screen (not the hold that opened it)
    case EVENT__LONG_PRESS_1:
      if(s_answerCallback && HoldButtons_isHeld(&s_hold, 0))
        s_answerCallback(false);
      return true;

    // Yes
    case EVENT__LONG_PRESS_2:
      if(s_answerCallback && HoldButtons_isHeld(&s_hold, 1))
        s_answerCallback(true);
      return true;

    default: break;
  }

  return false;
}


static void _draw(void) {  
  if(s_questionStr) {
    Gfx_drawStringAligned(Gfx_getWidth()/2, 8, (const char*)s_questionStr, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
  }

  // NO over button 1 (left), YES over button 2 (right). They fill while they are held.
  HoldButton_draw(HOLD_BUTTON_MARGIN, HOLD_BUTTON_W, "NO", HoldButtons_fill(&s_hold, 0));
  HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "YES", HoldButtons_fill(&s_hold, 1));
}