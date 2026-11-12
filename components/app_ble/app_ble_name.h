/***************************************************************************************************
 * @file    : app_ble_name.h
 * @brief   : The Bluetooth name for one Bluetooth start. No NimBLE headers: the simulator uses it too.
 **************************************************************************************************/

#ifndef _APP_BLE_NAME_H
#define _APP_BLE_NAME_H

// The longest Bluetooth name: the advertising packet (31 bytes) also has the flags (3 bytes) and
// the 128-bit service UUID (18 bytes); the name field adds 2 bytes. 31 - 3 - 18 - 2 = 8.
#define APP_BLE_NAME_MAX  8

// The name for the next AppBle_init() only (1 to APP_BLE_NAME_MAX characters; NULL or "" =
// "StatTrak"). AppBle_init() then forgets it: the next start uses "StatTrak" again. Only
// "Apps: SYNC" uses this. The update view and the accept view are always "StatTrak": the web
// updater looks for that name (tools/check_safety.py checks this).
void AppBle_setNextName(const char* name);

#endif // _APP_BLE_NAME_H
