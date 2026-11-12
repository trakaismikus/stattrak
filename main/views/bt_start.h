/***************************************************************************************************
 * @file    : bt_start.h
 * @brief   : The soft radio start of the Bluetooth screens (update, accept, app sync).
 *
 * The radio start pulls a current peak. With a bright display on a weak battery path, the two
 * together pulled the supply below the brown-out level, the chip restarted and the device switched
 * off (6 of 10 starts on one unit, also with firmware 1.0.1; 0 of 15 with the display off). So a
 * Bluetooth screen does not start the radio in its init:
 *   1. The screen slides in and shows only a small Bluetooth sign with waves (few lit pixels).
 *   2. When the display shows only the sign, the radio starts (the start function of the view).
 *   3. 0.4 s later the screen itself slides down from the top.
 * docs/BLUETOOTH.md, "Radio start".
 **************************************************************************************************/

#ifndef _BT_START_H
#define _BT_START_H

#include <stdint.h>
#include <stdbool.h>

#include "app_view.h"

#define BT_START_SETTLE_MS  30    // Frames with only the sign on the display before the radio starts
#define BT_START_SIGN_MS    400   // The sign stays this long after the radio start
#define BT_START_REVEAL_MS  300   // The screen slides in

// In the init of the view: `start` starts the radio (Ota_init() and what belongs to it) later
void BtStart_begin(view_id_t view, void (*start)(void));

// In the deinit of the view: no radio start after this
void BtStart_cancel(view_id_t view);

// Each loop (App_update): calls the start function at the right time
void BtStart_update(uint32_t now);

// In the draw of the view. true: the sign (and the slide) was drawn, do not draw the screen now.
// `screen` draws the screen of the view (used for the slide).
bool BtStart_draw(view_id_t view, void (*screen)(void));

// true while the view waits for its radio start (the radio is still off)
bool BtStart_isWaiting(view_id_t view);

#endif // _BT_START_H
