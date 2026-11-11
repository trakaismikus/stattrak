/**************************************************************************************************
 * Bluetooth commands for your own features. See ble_commands.h and docs/BLUETOOTH.md.
 *
 * This code runs in the main loop, the same as the views. It can read and change the counters
 * and the settings without special care.
 *
 * The app commands (0x0110 to 0x011A, docs/APPS.md section 6) work only while the view
 * "Apps: SYNC" is the current view. Each byte from the radio is checked.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "ble_commands.h"

#include <string.h>

#include "app.h"
#include "apps.h"
#include "app_sync_view.h"
#include "eeprom.h"
#include "ota.h"
#include "stapp.h"
#include "time.h"

/* > Defines **************************************************************************************/
#define ORDER_IDS_PER_PART  3

/* > Local Function Declarations ******************************************************************/
static uint8_t _handle(const ota_user_cmd_t* cmd, uint8_t* response, uint8_t* responseLen);
static uint8_t _handleApp(const ota_user_cmd_t* cmd, uint8_t* response, uint8_t* responseLen);
static uint8_t _setOrder(const uint8_t* data);
static uint8_t _appsStatus(apps_result_t result);
static void _put16(uint8_t* out, uint32_t value);
static void _put32(uint8_t* out, uint32_t value);
static uint16_t _get16(const uint8_t* in);
static uint32_t _get32(const uint8_t* in);

/* > Local Variables ******************************************************************************/
// SET_ORDER comes in parts
static uint32_t s_order[APPS_LIST_MAX];
static uint8_t  s_orderCount = 0;
static uint8_t  s_orderNextPart = 0;

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
  if(cmd->cmdId >= BLE_COMMAND__APP_INFO && cmd->cmdId <= BLE_COMMAND__APP_SETTING)
    return _handleApp(cmd, response, responseLen);

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

// The app manager. Only in the app sync view: nobody can change the apps in an other view.
static uint8_t _handleApp(const ota_user_cmd_t* cmd, uint8_t* response, uint8_t* responseLen) {
  const uint8_t* d = cmd->data;

  if(!AppSync_isActive())
    return BLE_COMMAND_STATUS__NOT_IN_APP_SYNC;

  // An install takes some seconds: no dim and no auto off while the phone talks to the device
  App_updateActivity(Time_getMillis());

  // While the person answers "INSTALL <NAME>?", nothing can change the apps
  const bool changes = (cmd->cmdId == BLE_COMMAND__APP_BEGIN || cmd->cmdId == BLE_COMMAND__APP_DATA ||
                        cmd->cmdId == BLE_COMMAND__APP_END || cmd->cmdId == BLE_COMMAND__APP_DELETE ||
                        cmd->cmdId == BLE_COMMAND__APP_SET_ORDER || cmd->cmdId == BLE_COMMAND__APP_SET_HIDDEN ||
                        (cmd->cmdId == BLE_COMMAND__APP_SETTING && d[5] != 0xFF));
  if(changes && AppSync_isWaiting())
    return BLE_COMMAND_STATUS__WAITING;

  switch(cmd->cmdId) {
    case BLE_COMMAND__APP_INFO: {
      response[0] = STAPP_ENGINE_VERSION;
      response[1] = STAPP_FORMAT_VERSION;
      _put16(&response[2], Apps_getFreeBytes());
      _put16(&response[4], STAPP_MAX_SIZE);
      response[6] = Apps_getInstalledCount();
      response[7] = APPS_MAX_INSTALLED;
      *responseLen = 8;
    } return BLE_COMMAND_STATUS__OK;

    case BLE_COMMAND__APP_LIST: {
      uint32_t id = 0;
      uint8_t flags = 0;
      if(!Apps_getEntry(d[0], &id, &flags))
        return BLE_COMMAND_STATUS__INDEX;

      const char* name = Apps_getName(id);
      _put32(&response[0], id);
      _put16(&response[4], Apps_getVersion(id));
      _put16(&response[6], Apps_getSize(id));
      response[8] = flags;
      response[9] = Apps_getSettingCount(id);
      for(uint8_t i = 0; i < 3; i++)
        response[10 + i] = (name != NULL && strlen(name) > i) ? (uint8_t)name[i] : 0;
      *responseLen = 13;
    } return BLE_COMMAND_STATUS__OK;

    case BLE_COMMAND__APP_NAME: {
      const char* name = Apps_getName(_get32(&d[0]));
      if(name == NULL)
        return BLE_COMMAND_STATUS__ERROR;

      const uint32_t length = (uint32_t)strlen(name);
      const uint32_t start = (uint32_t)d[4] * OTA_USER_RESPONSE_SIZE;
      uint32_t count = (start < length) ? length - start : 0;
      if(count > OTA_USER_RESPONSE_SIZE)
        count = OTA_USER_RESPONSE_SIZE;
      memcpy(response, &name[start < length ? start : length], count);
      *responseLen = (uint8_t)count;
    } return BLE_COMMAND_STATUS__OK;

    case BLE_COMMAND__APP_BEGIN: {
      switch(AppSync_begin(_get32(&d[0]), _get16(&d[4]), _get32(&d[6]))) {
        case APP_SYNC_OK:            return BLE_COMMAND_STATUS__OK;
        case APP_SYNC_ERR_NO_SPACE:  return BLE_COMMAND_STATUS__NO_SPACE;
        case APP_SYNC_ERR_TOO_LARGE: return BLE_COMMAND_STATUS__TOO_LARGE;
        case APP_SYNC_ERR_TOO_MANY:  return BLE_COMMAND_STATUS__TOO_MANY;
        case APP_SYNC_ERR_WAITING:   return BLE_COMMAND_STATUS__WAITING;
        default:                     return BLE_COMMAND_STATUS__ERROR;
      }
    }

    case BLE_COMMAND__APP_DATA: {
      uint16_t expected = 0;
      const app_sync_result_t result = AppSync_data(_get16(&d[0]), &d[2], OTA_USER_REQUEST_SIZE - 2, &expected);
      _put16(&response[0], expected);
      *responseLen = 2;
      if(result == APP_SYNC_OK)
        return BLE_COMMAND_STATUS__OK;
      return (result == APP_SYNC_ERR_OFFSET) ? BLE_COMMAND_STATUS__OFFSET : BLE_COMMAND_STATUS__ERROR;
    }

    case BLE_COMMAND__APP_END: {
      stapp_result_t reason = STAPP_OK;
      uint16_t expected = 0;
      switch(AppSync_end(&reason, &expected)) {
        case APP_SYNC_OK:
          return BLE_COMMAND_STATUS__OK;
        case APP_SYNC_ERR_OFFSET:
          _put16(&response[0], expected);
          *responseLen = 2;
          return BLE_COMMAND_STATUS__OFFSET;
        case APP_SYNC_ERR_CRC:
          return BLE_COMMAND_STATUS__BAD_CRC;
        case APP_SYNC_ERR_PACKAGE:
          response[0] = (uint8_t)reason;
          *responseLen = 1;
          return BLE_COMMAND_STATUS__BAD_PACKAGE;
        default:
          return BLE_COMMAND_STATUS__ERROR;
      }
    }

    case BLE_COMMAND__APP_STATUS:
      response[0] = (uint8_t)AppSync_getState();
      *responseLen = 1;
      return BLE_COMMAND_STATUS__OK;

    case BLE_COMMAND__APP_DELETE:
      return _appsStatus(Apps_delete(_get32(&d[0])));

    case BLE_COMMAND__APP_SET_ORDER:
      return _setOrder(d);

    case BLE_COMMAND__APP_SET_HIDDEN:
      if(d[4] > 1)
        return BLE_COMMAND_STATUS__ERROR;
      return _appsStatus(Apps_setHidden(_get32(&d[0]), d[4] == 1));

    case BLE_COMMAND__APP_SETTING: {
      const uint32_t id = _get32(&d[0]);
      const uint8_t setting = d[4];
      if(!Apps_isInstalled(id) || setting >= Apps_getSettingCount(id))
        return BLE_COMMAND_STATUS__ERROR;
      if(d[5] != 0xFF) {
        const uint8_t status = _appsStatus(Apps_setSetting(id, setting, d[5]));
        if(status != BLE_COMMAND_STATUS__OK)
          return status;
      }
      response[0] = Apps_getSetting(id, setting);
      *responseLen = 1;
    } return BLE_COMMAND_STATUS__OK;

    default:
      return BLE_COMMAND_STATUS__UNKNOWN;
  }
}

// SET_ORDER: u8 count, 3 x u32 id, u8 part. Parts 0, 1, 2, ... in order; the order is used
// when all ids are in. A part out of order starts again.
static uint8_t _setOrder(const uint8_t* data) {
  const uint8_t count = data[0];
  const uint8_t part = data[1 + 4 * ORDER_IDS_PER_PART];

  if(count == 0 || count > APPS_LIST_MAX || part != s_orderNextPart || (part > 0 && count != s_orderCount) ||
     (uint32_t)part * ORDER_IDS_PER_PART >= count) {
    s_orderNextPart = 0;
    return BLE_COMMAND_STATUS__ERROR;
  }

  s_orderCount = count;
  const uint8_t first = (uint8_t)(part * ORDER_IDS_PER_PART);
  for(uint8_t i = 0; i < ORDER_IDS_PER_PART && first + i < count; i++)
    s_order[first + i] = _get32(&data[1 + 4 * i]);

  if(first + ORDER_IDS_PER_PART < count) {
    s_orderNextPart++;
    return BLE_COMMAND_STATUS__OK;   // More parts come
  }

  s_orderNextPart = 0;
  return _appsStatus(Apps_setOrder(s_order, count));
}

static uint8_t _appsStatus(apps_result_t result) {
  switch(result) {
    case APPS_OK:             return BLE_COMMAND_STATUS__OK;
    case APPS_ERR_BUILTIN:    return BLE_COMMAND_STATUS__BUILTIN;
    case APPS_ERR_LAST_SHOWN: return BLE_COMMAND_STATUS__LAST_SHOWN;
    case APPS_ERR_NO_SPACE:   return BLE_COMMAND_STATUS__NO_SPACE;
    case APPS_ERR_TOO_LARGE:  return BLE_COMMAND_STATUS__TOO_LARGE;
    case APPS_ERR_TOO_MANY:   return BLE_COMMAND_STATUS__TOO_MANY;
    default:                  return BLE_COMMAND_STATUS__ERROR;
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

static uint16_t _get16(const uint8_t* in) {
  return (uint16_t)(in[0] | (in[1] << 8));
}

static uint32_t _get32(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}
