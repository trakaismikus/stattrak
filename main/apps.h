/***************************************************************************************************
 * @file    : apps.h
 * @brief   : The app list and the storage of installed apps (docs/APPS.md, sections 2 and 5).
 *
 * The list has the built-in apps (KILLS, KILLS + DEAD, STATS) and the installed apps, in menu
 * order, each shown or hidden. SETTINGS is not in this list: the app menu always adds it as the
 * last app (main/views/launcher_view.c), so the way to "Firmware: UPDATE" never depends on flash.
 *
 * Flash (NVS namespace "apps"): "list", and for each installed app p<id> (package),
 * v<id> (saved variables), s<id> (setting values), e<id> (error count). <id> = 8 hex digits.
 * The firmware writes to flash only when something changes.
 **************************************************************************************************/

#ifndef _APPS_H
#define _APPS_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "app_view.h"
#include "stapp.h"

/* > Defines **************************************************************************************/
#define APPS_MAX_INSTALLED    8
#define APPS_MAX_BYTES        (24 * 1024)   // All installed packages together
#define APPS_BUILTIN_COUNT    3
#define APPS_LIST_MAX         (APPS_BUILTIN_COUNT + APPS_MAX_INSTALLED)
#define APPS_MENU_MAX         (APPS_LIST_MAX + 1)   // 12: the list + SETTINGS
#define APPS_ERROR_LIMIT      3                     // App errors until the app is switched off
#define APPS_SAVE_PERIOD_MS   30000                 // Saved variables: at most once in 30 s (and at close)

#define APPS_DEVICE_NAME_MAX  8                     // The Bluetooth name (APP_BLE_NAME_MAX)

// Ids of the built-in apps. Installed apps have other ids (not 0, not 0xFFFFFFFF).
#define APPS_ID__NONE         0u
#define APPS_ID__KILLS        1u
#define APPS_ID__KILL_DEAD    2u
#define APPS_ID__STATS        3u

// Flags of an app (the LIST command sends them). Only HIDDEN is stored in the list.
#define APPS_FLAG__HIDDEN     0x01
#define APPS_FLAG__BUILTIN    0x02
#define APPS_FLAG__SETTINGS   0x04
#define APPS_FLAG__ERROR      0x08

/* > Type definitions *****************************************************************************/
typedef enum apps_result_e {
  APPS_OK = 0,
  APPS_ERR_UNKNOWN,       // No app with this id
  APPS_ERR_BUILTIN,       // A built-in app cannot be deleted
  APPS_ERR_LAST_SHOWN,    // The last shown app cannot be hidden
  APPS_ERR_NO_SPACE,      // Not enough of the 24 KB for packages
  APPS_ERR_TOO_LARGE,     // Larger than STAPP_MAX_SIZE
  APPS_ERR_TOO_MANY,      // 8 apps are installed
  APPS_ERR_BAD,           // Wrong data (id, order, setting, value)
  APPS_ERR_FLASH,         // The flash write failed
} apps_result_t;

// One app of the app menu
typedef struct apps_item_s {
  view_id_t view;         // VIEW_ID__APP_HOST for an installed app
  uint32_t  id;
} apps_item_t;

/* > Function Declarations ************************************************************************/

// At start (App_init): reads the list and checks each installed package. No flash writes.
void           Apps_init(void);
// Closes the flash handle (before a factory reset erases the flash). Apps_init() opens it again.
void           Apps_deinit(void);

// The menu: the shown apps in order (at least one: KILLS if all are hidden). Returns the count.
uint8_t        Apps_getMenu(apps_item_t* items, uint8_t max);

// The full list (with hidden apps), for the app manager
uint8_t        Apps_getCount(void);
bool           Apps_getEntry(uint8_t index, uint32_t* id, uint8_t* flags);
uint8_t        Apps_getInstalledCount(void);
uint32_t       Apps_getFreeBytes(void);

bool           Apps_isBuiltin(uint32_t id);
bool           Apps_isInstalled(uint32_t id);
uint8_t        Apps_getFlags(uint32_t id);                  // 0 for an unknown id
const char*    Apps_getName(uint32_t id);                   // NULL for an unknown id
uint16_t       Apps_getVersion(uint32_t id);
uint16_t       Apps_getSize(uint32_t id);
const uint8_t* Apps_getPicture(uint32_t id);                // 80 x 24 title picture, installed apps only

// Settings of an installed app
uint8_t        Apps_getSettingCount(uint32_t id);
uint8_t        Apps_getSettingValueCount(uint32_t id, uint8_t setting);
uint8_t        Apps_getSetting(uint32_t id, uint8_t setting);      // 0 if out of range
apps_result_t  Apps_setSetting(uint32_t id, uint8_t setting, uint8_t value);

// The package of an installed app from flash, checked again. STAPP_OK or the reason.
stapp_result_t Apps_loadPackage(uint32_t id, uint8_t* buffer, stapp_info_t* info);
// Saved variables: missing or the wrong size = all 0
void           Apps_loadValues(uint32_t id, int32_t* values, uint8_t count);
apps_result_t  Apps_saveValues(uint32_t id, const int32_t* values, uint8_t count);

// Changes from the app manager (Bluetooth, app sync view)
apps_result_t  Apps_canInstall(uint32_t id, uint32_t size);
apps_result_t  Apps_install(const uint8_t* package, const stapp_info_t* info);
apps_result_t  Apps_delete(uint32_t id);
apps_result_t  Apps_setHidden(uint32_t id, bool hidden);
apps_result_t  Apps_setOrder(const uint32_t* ids, uint8_t count);

// The Bluetooth name of "Apps: SYNC" that the person chose (docs/APPS.md section 6, DEVICE_NAME).
// `out` has APPS_DEVICE_NAME_MAX + 1 bytes; "" = none (the device is "StatTrak"). A factory reset
// erases it. Set: 1 to 8 characters 0x20 .. 0x7E, no space at the start or the end; "" = none.
void           Apps_getDeviceName(char* out);
apps_result_t  Apps_setDeviceName(const char* name);
bool           Apps_isDeviceNameValid(const char* name);

// One more run-time error of an installed app. At APPS_ERROR_LIMIT it is hidden and flagged.
void           Apps_noteError(uint32_t id);

// A 4 KB work buffer (STAPP_MAX_SIZE): the package that comes over Bluetooth, the app settings
// view. Not used by the app host (it has its own buffer).
uint8_t*       Apps_getWorkBuffer(void);

#endif // _APPS_H
