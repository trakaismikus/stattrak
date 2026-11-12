#ifndef _BLE_COMMANDS_H
#define _BLE_COMMANDS_H

/**************************************************************************************************
 * Bluetooth commands for your own features.
 *
 * A phone or a computer sends a command number and 16 data bytes. The device answers with a
 * status byte and 13 data bytes at most. docs/BLUETOOTH.md has the full description.
 *
 * To add a command:
 *   1. Add a number to the list below (0x0100 or larger).
 *   2. Add a "case" for it in _handle() in ble_commands.c.
 *   3. Add a test in tests/test_bluetooth.py. The simulator can send the command.
 *
 * The commands work only while Bluetooth is on. Bluetooth is on in the update view and in the
 * accept view. A view of your own can switch it on: see docs/BLUETOOTH.md.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include <stdint.h>

/* > Enumerations *********************************************************************************/
enum ble_command_e {
  // Answer: total kills (4 bytes), total deaths (4 bytes), game kills (2 bytes), game deaths (2 bytes).
  // All numbers have the low byte first. The game numbers stop at 65535.
  BLE_COMMAND__GET_COUNTERS = 0x0100,

  // App manager (docs/APPS.md, section 6). They work only in the view "Apps: SYNC" (status 3
  // elsewhere). All numbers have the low byte first.
  BLE_COMMAND__APP_INFO       = 0x0110,
  BLE_COMMAND__APP_LIST       = 0x0111,
  BLE_COMMAND__APP_NAME       = 0x0112,
  BLE_COMMAND__APP_BEGIN      = 0x0113,
  BLE_COMMAND__APP_DATA       = 0x0114,
  BLE_COMMAND__APP_END        = 0x0115,
  BLE_COMMAND__APP_STATUS     = 0x0116,
  BLE_COMMAND__APP_DELETE     = 0x0117,
  BLE_COMMAND__APP_SET_ORDER  = 0x0118,
  BLE_COMMAND__APP_SET_HIDDEN = 0x0119,
  BLE_COMMAND__APP_SETTING    = 0x011A,
  BLE_COMMAND__APP_ASK        = 0x011B,   // After BEGIN: the name; the device asks "INSTALL <NAME>?"
  BLE_COMMAND__APP_DEVICE_NAME = 0x011C,  // Read or set the Bluetooth name of "Apps: SYNC"

  // Add your commands here
};

enum ble_command_status_e {
  BLE_COMMAND_STATUS__OK      = 0,
  BLE_COMMAND_STATUS__UNKNOWN = 1,  // The firmware does not have this command
  BLE_COMMAND_STATUS__ERROR   = 2,  // The command data is not correct

  // App manager
  BLE_COMMAND_STATUS__NOT_IN_APP_SYNC = 3,   // Open Settings > "Apps: SYNC" on the device first
  BLE_COMMAND_STATUS__INDEX           = 4,   // LIST: the index is too large
  BLE_COMMAND_STATUS__NO_SPACE        = 5,
  BLE_COMMAND_STATUS__TOO_LARGE       = 6,
  BLE_COMMAND_STATUS__TOO_MANY        = 7,
  BLE_COMMAND_STATUS__OFFSET          = 8,   // Answer: u16 expected offset
  BLE_COMMAND_STATUS__BAD_CRC         = 9,
  BLE_COMMAND_STATUS__BAD_PACKAGE     = 10,  // Answer: u8 reason (stapp_result_t)
  BLE_COMMAND_STATUS__WAITING         = 11,  // The person must answer "INSTALL <NAME>?" first
  BLE_COMMAND_STATUS__BUILTIN         = 12,  // A built-in app cannot be deleted
  BLE_COMMAND_STATUS__LAST_SHOWN      = 13,  // The last shown app cannot be hidden
};

/* > Function Declarations ************************************************************************/

// Answers the commands that came over Bluetooth. Called from the main loop (App_update).
void BleCommands_update(void);

#endif // _BLE_COMMANDS_H
