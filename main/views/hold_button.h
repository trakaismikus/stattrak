/***************************************************************************************************
 * @file    : hold_button.h
 * @brief   : The buttons on the display: small (9 pixels high, font3x5), answered by a hold.
 *
 * A button on the display answers a hold of its round button (CONFIG_BUTTON_LONG_PRESS_TIME_MS,
 * 0.7 s), never a short press. While the person holds it, it fills from the left; it is full
 * when the long press comes. Only a hold that starts while the screen shows counts: a button
 * that is still down from the screen before does nothing (HoldButtons_reset at each new screen).
 * A press in the 0.3 s slide of a view change goes to the old view: the new screen ignores that
 * hold (no fill, no answer), and a new hold works. The repeats of a hold go only to the view where
 * the button went down (AppView_eventHandler).
 *
 * Use in a view:
 *   static hold_buttons_t s_hold;
 *   init / new screen:     HoldButtons_reset(&s_hold);
 *   eventHandler:          HoldButtons_event(&s_hold, event);
 *                          case EVENT__LONG_PRESS_2: if(HoldButtons_isHeld(&s_hold, 1)) { ... }
 *   draw:                  HoldButton_draw(x, w, "YES", HoldButtons_fill(&s_hold, 1));
 **************************************************************************************************/

#ifndef _HOLD_BUTTON_H
#define _HOLD_BUTTON_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define HOLD_BUTTON_Y       21    // The buttons are on the bottom line of the display
#define HOLD_BUTTON_H       9
#define HOLD_BUTTON_MARGIN  4     // Left button at x 4, right button ends at x 123
#define HOLD_BUTTON_W       22    // A button with 1 to 4 letters (NO, YES, OK, OPEN, BACK)

/* > Type Definitions *****************************************************************************/
typedef struct hold_buttons_s {
  bool     held[2];               // Button 1 / 2 is down, and it went down while this screen shows
  uint32_t start[2];
} hold_buttons_t;

/* > Function Declarations ************************************************************************/

// A new screen: holds that started before do not count
void    HoldButtons_reset(hold_buttons_t* hold);

// Give each button event (it uses EVENT__RAW_PRESS_x and EVENT__RAW_RELEASE_x)
void    HoldButtons_event(hold_buttons_t* hold, uint16_t event);

// button 0 = button 1 (left), 1 = button 2 (right). true: the hold started on this screen.
bool    HoldButtons_isHeld(const hold_buttons_t* hold, uint8_t button);

// 0 .. 100: how much of the hold is done (100 when the long press comes)
uint8_t HoldButtons_fill(const hold_buttons_t* hold, uint8_t button);

// The fill of a button that either round button answers (a button in the middle)
uint8_t HoldButtons_fillAny(const hold_buttons_t* hold);

// Draws a button at x, HOLD_BUTTON_Y: the frame of GUI_DRAW_BTN, text in font3x5. `fill` inverts
// it from the left (100 = the look of a pressed button).
void    HoldButton_draw(int16_t x, int16_t w, const char* text, uint8_t fill);

// x of a button that ends HOLD_BUTTON_MARGIN before the right edge, and of a centered button
#define HOLD_BUTTON_RIGHT_X(w)    ((int16_t)(128 - HOLD_BUTTON_MARGIN - (w)))
#define HOLD_BUTTON_CENTER_X(w)   ((int16_t)((128 - (w)) / 2))

#endif // _HOLD_BUTTON_H
