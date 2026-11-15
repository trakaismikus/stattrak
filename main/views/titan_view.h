/***************************************************************************************************
 * @file    : titan_view.h
 * @brief   : The TITAN app: pairing, the PLAY and SESSION screens and the alerts.
 *            qa/titan-app/decisions.md is the specification. The logic is in main/titan.c.
 **************************************************************************************************/

#ifndef _TITAN_VIEW_H
#define _TITAN_VIEW_H

#include <stdint.h>
#include <stdbool.h>

// Each loop (App_update): radio events and screen changes while TITAN is open
void TitanView_update(uint32_t now);

// The device switches off (the view gets no deinit then): save the count
void TitanView_onShutdown(void);

// For the simulator and the tests: the screen of the view (titan_screen_t)
uint8_t     TitanView_getScreen(void);
const char* TitanView_getScreenName(void);
const char* TitanView_getPinEntry(void);    // The letters of the PIN wheel
uint8_t     TitanView_getPinPos(void);

#endif // _TITAN_VIEW_H
