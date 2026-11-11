/**************************************************************************************************
 * Bluetooth commands for your own features. See ble_commands.h and docs/BLUETOOTH.md.
 *
 * This code runs in the main loop, the same as the views. It can read and change the counters
 * and the settings without special care.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "ble_commands.h"

#include <string.h>

#include "app.h"
#include "eeprom.h"
#include "ota.h"

/* > Local Function Declarations ******************************************************************/
static uint8_t _handle(const ota_user_cmd_t* cmd, uint8_t* response, uint8_t* responseLen);
static void _put16(uint8_t* out, uint32_t value);
static void _put32(uint8_t* out, uint32_t value);

/* > Function Definitions *************************************************************************/

void BleCommands_update(void) {
  ota_user_cmd_t cmd;

  // A few commands per loop at most, so a phone that sends many commands cannot stop the views
  for(uint8_t i = 0; i < 4 && Ota_getUserCommand(&cmd); i++) {
    uint8_t response[OTA_USER_RESPONSE_SIZE] = { 0 };
    uint8_t responseLen = 0;

    const uint8_t status = _handle(&cmd, response, &responseLen);
    if(responseLen > OTA_USER_RESPONSE_SIZE)
      responseLen = OTA_USER_RESPONSE_SIZE;

    Ota_sendUserResponse(cmd.cmdId, status, response, responseLen);
  }
}

/* > Local Function Definitions *******************************************************************/

// Does one command. Writes the answer data into `response` (OTA_USER_RESPONSE_SIZE bytes at
// most) and its length into `responseLen`. Returns the status.
static uint8_t _handle(const ota_user_cmd_t* cmd, uint8_t* response, uint8_t* responseLen) {
  switch(cmd->cmdId) {
    case BLE_COMMAND__GET_COUNTERS: {
      const uint32_t gameKills = Eeprom_read(ENTRY_ID__GAME_KILLS);
      const uint32_t gameDeaths = Eeprom_read(ENTRY_ID__GAME_DEATHS);

      _put32(&response[0], Eeprom_read(ENTRY_ID__TOTAL_KILLS));
      _put32(&response[4], Eeprom_read(ENTRY_ID__TOTAL_DEATHS));
      _put16(&response[8], gameKills);
      _put16(&response[10], gameDeaths);
      *responseLen = 12;
    } return BLE_COMMAND_STATUS__OK;

    // Add your commands here

    default:
      return BLE_COMMAND_STATUS__UNKNOWN;
  }
}

static void _put16(uint8_t* out, uint32_t value) {
  if(value > 0xFFFF)
    value = 0xFFFF;

  out[0] = (uint8_t)(value & 0xFF);
  out[1] = (uint8_t)(value >> 8);
}

static void _put32(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xFF);
  out[1] = (uint8_t)((value >> 8) & 0xFF);
  out[2] = (uint8_t)((value >> 16) & 0xFF);
  out[3] = (uint8_t)(value >> 24);
}
