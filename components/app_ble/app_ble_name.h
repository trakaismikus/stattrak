/***************************************************************************************************
 * @file    : app_ble_name.h
 * @brief   : The Bluetooth name for one Bluetooth start. No NimBLE headers: the simulator uses it too.
 **************************************************************************************************/

#ifndef _APP_BLE_NAME_H
#define _APP_BLE_NAME_H

// The longest Bluetooth name: "StatTrak | " + 12 characters = 23. The advertising packet has 31
// bytes:
// the flags (3 bytes), the name field (2 bytes + the name) and the 128-bit service UUID (18 bytes).
// A name of 8 characters or less fits with the UUID (31 - 3 - 18 - 2 = 8). A longer name moves
// the UUID to the scan response (app_ble.c). "StatTrak" (the update views) never moves it.
#define APP_BLE_NAME_MAX         23
#define APP_BLE_NAME_WITH_UUID   8

// The name for the next AppBle_init() only (1 to APP_BLE_NAME_MAX characters; NULL or "" =
// "StatTrak"). AppBle_init() then forgets it: the next start uses "StatTrak" again. Only
// the app sync view uses this. The update view and the accept view are always "StatTrak": the web
// updater looks for that name (tools/check_safety.py checks this).
void AppBle_setNextName(const char* name);

#endif // _APP_BLE_NAME_H
