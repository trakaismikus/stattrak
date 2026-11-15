/***************************************************************************************************
 * @file    : app_data.h
 * @brief   : The data store of each installed app: rows of numbers that the app adds with log()
 *            and the sync page reads (docs/DATA.md section 2).
 *
 * One app at a time is in RAM (4 KB). The app host opens the store of the app that runs; the
 * Bluetooth commands of the sync page load the store that they read. Changed parts go to flash
 * in AppData_update() (at most once in 30 s), at the close of the app and at power-off. Never in
 * a draw function.
 **************************************************************************************************/

#ifndef _APP_DATA_H
#define _APP_DATA_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define APP_DATA_APP_VALUES       1024    // 4096 bytes for each app
#define APP_DATA_TOTAL_VALUES     2048    // 8192 bytes for all apps together
#define APP_DATA_CHUNK_VALUES     64      // One flash blob: 256 bytes
#define APP_DATA_MAX_COLUMNS      4
#define APP_DATA_RESERVE_ENTRIES  160     // NVS entries (32 bytes) that stay free for settings, counters, installs
#define APP_DATA_FLUSH_MS         30000

/* > Function Declarations ************************************************************************/

// The store of the app that runs (app host). `columns` 1..4 from the package; 0 = no data store.
void     AppData_open(uint32_t id, uint8_t columns);
void     AppData_closeLater(void);                           // The app closes: the next AppData_update() writes
bool     AppData_log(const int32_t* values);                 // One row of `columns` values. false = full
uint16_t AppData_rows(void);
int32_t  AppData_get(int32_t row, int32_t column);           // 0 outside the rows / columns
void     AppData_clear(void);                                // All rows of the open app

// Each loop (App_update): the changed parts to flash, at most once in 30 s
void     AppData_update(uint32_t now);
// Now (power-off)
void     AppData_flush(void);

// Any app (the sync page). `columns` 0 = no store yet (the package columns are in apps.c).
bool     AppData_info(uint32_t id, uint8_t* columns, uint16_t* rows);
uint16_t AppData_fitRows(uint32_t id, uint8_t columns);       // Rows that fit now: the app size or what is left of all
uint8_t  AppData_readValues(uint32_t id, uint16_t first, int32_t* out, uint8_t max);
void     AppData_erase(uint32_t id);                         // The app is deleted, or DATA_CLEAR
uint32_t AppData_usedBytes(void);                            // All apps together

// The flash was erased (factory reset): forget the store in RAM
void     AppData_forget(void);

#endif // _APP_DATA_H
