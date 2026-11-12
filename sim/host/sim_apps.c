/***************************************************************************************************
 * @file    : sim_apps.c
 * @brief   : Install an app package in the simulator (see sim_apps.h).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "sim_apps.h"

#include <stdio.h>
#include <string.h>

#include "host.h"
#include "app_view.h"
#include "app_host_view.h"
#include "ble_commands.h"
#include "stapp.h"

/* > Defines **************************************************************************************/
#define DATA_BYTES        14      // DATA: u16 offset + 14 bytes of the package
#define MAX_WAITING       4       // Commands sent and not answered yet (the command queue)
#define STATUS_EVERY_MS   200     // STATUS while the device shows the question
#define OPEN_TIMEOUT_MS   8000    // Also the time of the start screen after a power-on

// STATUS answer (docs/APPS.md section 6)
#define INSTALL_STATE__IDLE       0
#define INSTALL_STATE__INSTALLED  3
#define INSTALL_STATE__REFUSED    4
#define INSTALL_STATE__TIMEOUT    5

/* > Local Variables ******************************************************************************/
static sim_apps_state_t s_state = SIM_APPS__IDLE;
static char     s_text[120] = "";
static char     s_name[STAPP_MAX_NAME + 1] = "";
static uint8_t  s_package[STAPP_MAX_SIZE];
static uint32_t s_size = 0;
static uint32_t s_id = 0;
static bool     s_open = false;
static uint32_t s_sentBytes = 0;      // DATA sent up to here
static uint8_t  s_waiting = 0;        // Commands without an answer
static bool     s_endSent = false;
static uint64_t s_startMs = 0;
static uint64_t s_lastStatusMs = 0;

/* > Local Functions ******************************************************************************/

static void _fail(const char* text) {
  s_state = SIM_APPS__FAILED;
  snprintf(s_text, sizeof(s_text), "%s", text);
}

static void _put32(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static bool _send(uint16_t cmd, const uint8_t* data, uint8_t len) {
  if(!HostOta_sendUserCommand(cmd, data, len))
    return false;
  s_waiting++;
  return true;
}

// Views where the simulator may open "Apps: SYNC" (not during an update, a warning or power-off)
static bool _canOpenSync(view_id_t view) {
  switch(view) {
    case VIEW_ID__TOTAL:
    case VIEW_ID__KILL_DEAD:
    case VIEW_ID__STATS:
    case VIEW_ID__SETTINGS:
    case VIEW_ID__LAUNCHER:
    case VIEW_ID__APP_HOST:
    case VIEW_ID__APP_SETTINGS:
    case VIEW_ID__APP_SYNC:
      return true;
    default:
      return false;
  }
}

// The start screen after a power-on: wait for its end
static bool _isStarting(view_id_t view) {
  return view == VIEW_ID__BLANK || view == VIEW_ID__INTRO;
}

static const char* _statusText(uint8_t status) {
  switch(status) {
    case BLE_COMMAND_STATUS__UNKNOWN:         return "this firmware has no app commands";
    case BLE_COMMAND_STATUS__ERROR:           return "the device did not accept the data";
    case BLE_COMMAND_STATUS__NOT_IN_APP_SYNC: return "the device left Apps: SYNC";
    case BLE_COMMAND_STATUS__NO_SPACE:        return "not enough space on the device: delete an app first";
    case BLE_COMMAND_STATUS__TOO_LARGE:       return "the app is too large";
    case BLE_COMMAND_STATUS__TOO_MANY:        return "too many apps on the device: delete an app first";
    case BLE_COMMAND_STATUS__OFFSET:          return "a part of the app was lost";
    case BLE_COMMAND_STATUS__BAD_CRC:         return "the app was damaged on the way";
    case BLE_COMMAND_STATUS__BAD_PACKAGE:     return "this is not a valid app file";
    case BLE_COMMAND_STATUS__WAITING:         return "the device waits for an answer: press a button first";
    default:                                  return "the device said no";
  }
}

static void _installed(void) {
  s_state = SIM_APPS__INSTALLED;
  if(s_open) {
    // As the app menu does when it opens an app
    AppHost_setApp(s_id);
    AppView_set(VIEW_ID__APP_HOST);
    snprintf(s_text, sizeof(s_text), "%s is installed and open. LEFT + RIGHT: app menu.", s_name);
  } else {
    snprintf(s_text, sizeof(s_text), "Installed. Hold LEFT to leave Apps: SYNC, then LEFT + RIGHT for the app menu.");
  }
}

// Reads the answers of the device. false: the install ended (an error, or the answer of STATUS).
static bool _readAnswers(void) {
  uint16_t cmd = 0;
  uint8_t status = 0, len = 0;
  uint8_t data[16];

  while(HostOta_takeUserResponse(&cmd, &status, data, &len)) {
    if(s_waiting > 0)
      s_waiting--;

    if(status != BLE_COMMAND_STATUS__OK) {
      char text[100];
      snprintf(text, sizeof(text), "Not installed: %s.", _statusText(status));
      _fail(text);
      return false;
    }

    if(cmd == BLE_COMMAND__APP_STATUS && len >= 1) {
      if(data[0] == INSTALL_STATE__INSTALLED) {
        _installed();
        return false;
      }
      if(data[0] == INSTALL_STATE__REFUSED || data[0] == INSTALL_STATE__TIMEOUT) {
        s_state = SIM_APPS__REFUSED;
        snprintf(s_text, sizeof(s_text), "Not installed: the device said no.");
        return false;
      }
      if(data[0] == INSTALL_STATE__IDLE) {
        _fail("Not installed: the device stopped the install.");
        return false;
      }
    }

    if(cmd == BLE_COMMAND__APP_END) {
      s_state = SIM_APPS__QUESTION;
      if(s_open)
        snprintf(s_text, sizeof(s_text), "INSTALL %s? The simulator holds RIGHT (button 2) = yes...", s_name);
      else
        snprintf(s_text, sizeof(s_text), "The device asks: hold RIGHT (button 2) = yes, hold LEFT (button 1) = no.");
    }
  }
  return true;
}

// false (and FAILED): the device is not in "Apps: SYNC" any more (a warning, power-off, a button)
static bool _stillInSync(void) {
  if(HostOta_isActive() && AppView_get() == VIEW_ID__APP_SYNC && AppView_getNext() == VIEW_ID__APP_SYNC)
    return true;
  _fail("Not installed: the device left Apps: SYNC.");
  return false;
}

/* > Global Functions *****************************************************************************/

bool SimApps_start(const uint8_t* package, uint32_t size, bool open) {
  if(s_state == SIM_APPS__OPENING || s_state == SIM_APPS__SENDING || s_state == SIM_APPS__QUESTION) {
    // The install that runs goes on; its text stays
    return false;
  }

  if(size < STAPP_HEADER_SIZE || size > sizeof(s_package)) {
    _fail("Not installed: this is not a valid app file.");
    return false;
  }

  memcpy(s_package, package, size);
  s_size = size;
  memcpy(&s_id, &s_package[8], sizeof(s_id));
  memcpy(s_name, &s_package[16], STAPP_MAX_NAME);
  s_name[STAPP_MAX_NAME] = '\0';
  s_open = open;
  s_sentBytes = 0;
  s_waiting = 0;
  s_endSent = false;
  s_startMs = Host_nowMs();
  s_lastStatusMs = 0;
  s_state = SIM_APPS__OPENING;
  snprintf(s_text, sizeof(s_text), "Opening Apps: SYNC...");

  // Old answers of an install that stopped must not count for this one
  uint16_t cmd;
  uint8_t status, len, data[16];
  while(HostOta_takeUserResponse(&cmd, &status, data, &len)) { }
  return true;
}

void SimApps_update(void) {
  switch(s_state) {
    case SIM_APPS__OPENING: {
      const view_id_t view = AppView_get();
      const view_id_t next = AppView_getNext();
      if((Host_nowMs() - s_startMs) > OPEN_TIMEOUT_MS) {
        _fail("Not installed: Apps: SYNC did not open.");
        return;
      }
      if(_isStarting(view) || _isStarting(next))
        return;
      if(next != VIEW_ID__APP_SYNC) {
        if(!_canOpenSync(view) || !_canOpenSync(next)) {
          _fail("Not installed: the device is busy (update, warning or power-off). Try again.");
          return;
        }
        AppView_set(VIEW_ID__APP_SYNC);   // As the Settings row "Apps: SYNC" does
        return;
      }
      if(view == VIEW_ID__APP_SYNC && HostOta_isActive()) {
        // BEGIN: u32 id, u16 size, u32 CRC-32 of the whole package
        uint8_t data[10];
        _put32(&data[0], s_id);
        data[4] = (uint8_t)s_size;
        data[5] = (uint8_t)(s_size >> 8);
        _put32(&data[6], Stapp_crc32(s_package, s_size));
        if(_send(BLE_COMMAND__APP_BEGIN, data, sizeof(data))) {
          s_state = SIM_APPS__SENDING;
          snprintf(s_text, sizeof(s_text), "Sending %s...", s_name);
        }
      }
    } break;

    case SIM_APPS__SENDING:
      if(!_readAnswers() || s_state != SIM_APPS__SENDING || !_stillInSync())
        return;
      while(s_waiting < MAX_WAITING && s_sentBytes < s_size) {
        uint8_t data[2 + DATA_BYTES];
        const uint32_t count = (s_size - s_sentBytes < DATA_BYTES) ? (s_size - s_sentBytes) : DATA_BYTES;
        data[0] = (uint8_t)s_sentBytes;
        data[1] = (uint8_t)(s_sentBytes >> 8);
        memcpy(&data[2], &s_package[s_sentBytes], count);
        if(!_send(BLE_COMMAND__APP_DATA, data, (uint8_t)(2 + count)))
          return;
        s_sentBytes += count;
      }
      // END after the answers of all DATA commands
      if(s_sentBytes >= s_size && s_waiting == 0 && !s_endSent && _send(BLE_COMMAND__APP_END, NULL, 0))
        s_endSent = true;
      break;

    case SIM_APPS__QUESTION:
      if(!_readAnswers() || s_state != SIM_APPS__QUESTION || !_stillInSync())
        return;
      if(s_waiting == 0 && (Host_nowMs() - s_lastStatusMs) >= STATUS_EVERY_MS) {
        s_lastStatusMs = Host_nowMs();
        _send(BLE_COMMAND__APP_STATUS, NULL, 0);
      }
      break;

    default:
      break;
  }
}

sim_apps_state_t SimApps_getState(void) {
  return s_state;
}

const char* SimApps_getText(void) {
  return s_text;
}

bool SimApps_wantsYes(void) {
  return s_open && s_state == SIM_APPS__QUESTION;
}
