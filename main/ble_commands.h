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

  // Add your commands here
};

enum ble_command_status_e {
  BLE_COMMAND_STATUS__OK      = 0,
  BLE_COMMAND_STATUS__UNKNOWN = 1,  // The firmware does not have this command
  BLE_COMMAND_STATUS__ERROR   = 2,  // The command data is not correct
};

/* > Function Declarations ************************************************************************/

// Answers the commands that came over Bluetooth. Called from the main loop (App_update).
void BleCommands_update(void);

#endif // _BLE_COMMANDS_H
