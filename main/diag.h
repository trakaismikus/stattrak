/***************************************************************************************************
 * @file    : diag.h
 * @brief   : TEST FIRMWARE ONLY (branch diag/bt-memory, never merged): the free RAM at each
 *            Bluetooth start, to find why a Bluetooth start sometimes turns the device off.
 *
 * Before each Bluetooth start (update view, Apps: SYNC) the free heap and the largest free block
 * are written to flash with the mark "started". 3 s later, if the device still runs, the start
 * counts as OK and the free heap after the start is written too. A mark "started" that is still
 * there at the next power-on = that start turned the device off.
 * Settings shows it: "Mem" (now), "BT ok" (last good start), "BT off" (last start that failed).
 **************************************************************************************************/

#ifndef _DIAG_H
#define _DIAG_H

#include <stdint.h>

void Diag_init(void);                   // App_init: count a start that did not end
void Diag_btStart(void);                // Right before Ota_init()
void Diag_update(uint32_t now);         // Each loop: marks the start OK after 3 s

// Texts for the settings rows (updated by Diag_refresh)
extern char g_diagNow[16];
extern char g_diagOk[16];
extern char g_diagFail[16];
void Diag_refresh(void);

#endif // _DIAG_H
