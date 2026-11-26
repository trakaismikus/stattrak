/***************************************************************************************************
 * @file    : hold_button.c
 * @brief   : The buttons on the display. See hold_button.h.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "hold_button.h"

#include "app.h"
#include "gfx.h"
#include "font.h"
#include "font_glyph.h"
#include "time.h"
#include "sdkconfig.h"

/* > Defines **************************************************************************************/
#define HOLD_MS   CONFIG_BUTTON_LONG_PRESS_TIME_MS   // The fill is full when the long press comes

/* > Function Definitions *************************************************************************/

void HoldButtons_reset(hold_buttons_t* hold) {
  hold->held[0] = hold->held[1] = false;
}

void HoldButtons_event(hold_buttons_t* hold, uint16_t event) {
  switch(event) {
    case EVENT__RAW_PRESS_1:
    case EVENT__RAW_PRESS_2: {
      const uint8_t button = (event == EVENT__RAW_PRESS_1) ? 0 : 1;
      hold->held[button] = true;
      hold->start[button] = Time_getMillis();
    } break;

    case EVENT__RAW_RELEASE_1:
      hold->held[0] = false;
      break;

    case EVENT__RAW_RELEASE_2:
      hold->held[1] = false;
      break;

    default: break;
  }
}

bool HoldButtons_isHeld(const hold_buttons_t* hold, uint8_t button) {
  return button < 2 && hold->held[button];
}

uint8_t HoldButtons_fill(const hold_buttons_t* hold, uint8_t button) {
  if(!HoldButtons_isHeld(hold, button))
    return 0;
  const uint32_t held = Time_getMillis() - hold->start[button];
  return (held >= HOLD_MS) ? 100 : (uint8_t)((held * 100u) / HOLD_MS);
}

uint8_t HoldButtons_fillAny(const hold_buttons_t* hold) {
  const uint8_t a = HoldButtons_fill(hold, 0);
  const uint8_t b = HoldButtons_fill(hold, 1);
  return (a > b) ? a : b;
}

void HoldButton_draw(int16_t x, int16_t w, const char* text, uint8_t fill) {
  const int16_t y = HOLD_BUTTON_Y;
  Gfx_drawVLine(x, y + 1, HOLD_BUTTON_H - 2, 1);
  Gfx_drawVLine(x + w - 1, y + 1, HOLD_BUTTON_H - 2, 1);
  Gfx_drawHLine(x + 1, y, w - 2, 1);
  Gfx_drawHLine(x + 1, y + HOLD_BUTTON_H - 1, w - 2, 1);
  Gfx_drawStringAligned(x + w / 2, y + 2, text, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  const int16_t width = (int16_t)(((w - 2) * fill + 50) / 100);
  if(width > 0)
    Gfx_invertRect(x + 1, y + 1, width, HOLD_BUTTON_H - 2);
}

void HoldButton_drawLarge(int16_t x, int16_t y, int16_t w, int16_t h, const char* text, uint8_t fill) {
  Gfx_drawRoundRect(x, y, w, h, 2, 1);
  Gfx_drawStringScaledAligned(x + w / 2, y + (h - 2 * 7) / 2, text, &kFontNightverbBig, 1, 2, 0, GFX_ALIGN__CENTER);

  // Inside the round corners
  const int16_t width = (int16_t)(((w - 4) * fill + 50) / 100);
  if(width > 0)
    Gfx_invertRect(x + 2, y + 2, width, h - 4);
}
