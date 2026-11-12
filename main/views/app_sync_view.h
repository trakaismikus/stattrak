/***************************************************************************************************
 * @file    : app_sync_view.h
 * @brief   : Settings > "Apps: SYNC": Bluetooth on for the app manager of the web updater.
 *
 * In this view the device refuses new firmware (Ota_setUpdateAllowed(false)), and only here the
 * app commands of main/ble_commands.c work. The device asks "INSTALL <NAME>?" before the data
 * comes (BEGIN, ASK); only after the person holds button 2 it takes the data, and END writes the
 * checked package to flash once.
 **************************************************************************************************/

#ifndef _APP_SYNC_VIEW_H
#define _APP_SYNC_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "stapp.h"

/* > Defines **************************************************************************************/
#define APP_SYNC_QUESTION_MS    30000   // No answer to "INSTALL <NAME>?" in this time = no
#define APP_SYNC_DATA_MS        10000   // No data for this time while receiving = the transfer stopped

/* > Enumerations *********************************************************************************/
// Install state (the STATUS command sends it). Values are stable: the web page uses them.
typedef enum app_sync_state_e {
  APP_SYNC_STATE__IDLE = 0,
  APP_SYNC_STATE__RECEIVING,    // The person said yes: the data comes
  APP_SYNC_STATE__WAITING,      // "INSTALL <NAME>?" on the display
  APP_SYNC_STATE__INSTALLED,
  APP_SYNC_STATE__REFUSED,
  APP_SYNC_STATE__TIMEOUT,
} app_sync_state_t;

typedef enum app_sync_result_e {
  APP_SYNC_OK = 0,
  APP_SYNC_ERR_DATA,            // Wrong data, or the command does not fit the install state
  APP_SYNC_ERR_NO_SPACE,
  APP_SYNC_ERR_TOO_LARGE,
  APP_SYNC_ERR_TOO_MANY,
  APP_SYNC_ERR_OFFSET,          // Not the expected offset
  APP_SYNC_ERR_CRC,
  APP_SYNC_ERR_PACKAGE,         // The package check failed
  APP_SYNC_ERR_WAITING,         // The person must answer the install question first
  APP_SYNC_ERR_FLASH,           // The flash write failed
} app_sync_result_t;

/* > Function Declarations ************************************************************************/

// true while the app sync view is the current view: only then the app commands work
bool              AppSync_isActive(void);

app_sync_state_t  AppSync_getState(void);
bool              AppSync_isWaiting(void);

// The install steps of the Bluetooth commands
app_sync_result_t AppSync_begin(uint32_t id, uint32_t size, uint32_t crc);
// The name field of ASK (16 bytes, as in the package header): shows "INSTALL <NAME>?"
app_sync_result_t AppSync_ask(const uint8_t* name, uint8_t length);
app_sync_result_t AppSync_data(uint16_t offset, const uint8_t* data, uint8_t length, uint16_t* expected);
app_sync_result_t AppSync_end(stapp_result_t* reason, uint16_t* expected);

// Each loop (App_update): the 30 s limit of the install question, and a transfer that stopped
void              AppSync_update(uint32_t now);

#endif // _APP_SYNC_VIEW_H
