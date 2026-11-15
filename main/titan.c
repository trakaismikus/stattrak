/***************************************************************************************************
 * @file    : titan.c
 * @brief   : The TITAN app without the screens (see titan.h).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "titan.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "app.h"
#include "gate_ble.h"

/* > Defines **************************************************************************************/
#define TAG                 "Titan"
#define NVS_NAMESPACE       "titan"
#define NVS_KEY             "cfg"
#define CFG_VERSION         1
#define CFG_SIZE            32

#define CONNECT_TRY_MS      10000   // One search for the paired ETU
#define RETRY_MS            500     // Wait before the next search
#define START_RETRY_MS      200     // The radio is not ready yet: try again
#define SCAN_START_LIMIT_MS 5000    // The radio did not get ready for the search

/* > Type Definitions *****************************************************************************/
typedef struct cfg_s {
  char     serial[GATE_SERIAL_LEN + 1];   // "" = not paired
  char     pin[GATE_PIN_LEN + 1];
  uint8_t  magSize;
  uint8_t  ammoAlert;
  uint8_t  battery;
  uint16_t rounds;                        // Rounds left (MAG SIZE set)
  uint32_t sessionShots;
  uint16_t reloads;
  uint32_t sessionMs;                     // Time with the app open, before this open
} cfg_t;

typedef enum phase_e {
  PHASE__OFF = 0,
  PHASE__WAIT,        // Wait until s_retryAt, then search
  PHASE__FINDING,     // GateBle_connect() runs
  PHASE__PIN,         // Connected, the PIN is sent
  PHASE__READY,
  PHASE__WRONG_PIN,
} phase_t;

/* > Local Function Declarations ******************************************************************/
static void _save(void);
static void _defaults(void);
static void _handleEvent(const gate_ble_event_t* event, uint32_t now);
static void _applyInfo(const gate_info_t* info, bool notification, uint32_t now);
static void _addFound(const char* serial, int8_t rssi);
static void _updateLink(uint32_t now);
static void _updateAlerts(uint32_t now);
static void _lost(uint32_t now);

/* > Local Variables ******************************************************************************/
static const struct {
  const char* name;
  uint16_t    limitMv;
} kBatteries[TITAN_BATT_COUNT] = {
  { "2S LIPO",  7000 },    // 3.5 V a cell
  { "3S LIPO",  10500 },
  { "2S LIFE",  6200 },    // 3.1 V a cell
  { "3S LIFE",  9300 },
  { "8.4 NIMH", 7700 },    // 7 cells, 1.1 V a cell
  { "9.6 NIMH", 8800 },    // 8 cells
  { "OFF",      0 },
};

static const uint8_t kAmmoAlertPercent[TITAN_AMMO_ALERT_COUNT] = { 0, 10, 20, 30 };

static cfg_t     s_cfg;
static bool      s_loaded = false;

// Session
static uint32_t  s_openedAt = 0;
static bool      s_open = false;
static bool      s_uncertain = false;

// Search for the pick list
static bool          s_scanning = false;
static bool          s_scanStarted = false;
static uint32_t      s_scanAskedAt = 0;
static titan_found_t s_found[TITAN_FOUND_MAX];
static uint8_t       s_foundCount = 0;

// Connection
static phase_t   s_phase = PHASE__OFF;
static char      s_linkSerial[GATE_SERIAL_LEN + 1];
static char      s_linkPin[GATE_PIN_LEN + 1];
static uint32_t  s_retryAt = 0;
static uint32_t  s_pinSentAt = 0;
static uint32_t  s_lastReadAt = 0;
static bool      s_wasReady = false;
static bool      s_lost = false;         // The connection was lost (after READY) and is not back
static uint32_t  s_lostAt = 0;           // The time of the lost connection

// ETU state
static gate_info_t s_info;
static bool      s_haveInfo = false;
static bool      s_low = false;          // The voltage is under the limit
static uint32_t  s_lowSince = 0;         // ... since this time

// Alerts
static uint8_t   s_activeAlerts = 0;
static uint8_t   s_pendingAlerts = 0;

/* > Function Definitions *************************************************************************/

// --- Settings and pairing

void Titan_load(void) {
  uint8_t buf[CFG_SIZE];
  size_t length = sizeof(buf);
  nvs_handle_t handle;
  bool good = false;

  _defaults();
  if(nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    good = nvs_get_blob(handle, NVS_KEY, buf, &length) == ESP_OK && length == sizeof(buf) && buf[0] == CFG_VERSION;
    nvs_close(handle);
  }

  if(good) {
    // Each value is checked: the flash can be damaged or from an other firmware
    bool serialGood = true;
    for(uint8_t i = 0; i < GATE_SERIAL_LEN; i++)
      serialGood = serialGood && Gate_isPinChar((char)buf[1 + i]) && Gate_isPinChar((char)buf[7 + i]);
    if(serialGood) {
      memcpy(s_cfg.serial, &buf[1], GATE_SERIAL_LEN);
      memcpy(s_cfg.pin, &buf[7], GATE_PIN_LEN);
      s_cfg.serial[GATE_SERIAL_LEN] = '\0';
      s_cfg.pin[GATE_PIN_LEN] = '\0';
    }
    if(buf[13] <= TITAN_MAG_STEPS)        s_cfg.magSize = buf[13];
    if(buf[14] < TITAN_AMMO_ALERT_COUNT)  s_cfg.ammoAlert = buf[14];
    if(buf[15] < TITAN_BATT_COUNT)        s_cfg.battery = buf[15];
    const uint16_t rounds = (uint16_t)(buf[16] | (buf[17] << 8));
    s_cfg.rounds = (rounds <= Titan_getCapacity()) ? rounds : Titan_getCapacity();
    s_cfg.sessionShots = (uint32_t)buf[18] | ((uint32_t)buf[19] << 8) | ((uint32_t)buf[20] << 16) | ((uint32_t)buf[21] << 24);
    s_cfg.reloads = (uint16_t)(buf[22] | (buf[23] << 8));
    s_cfg.sessionMs = (uint32_t)buf[24] | ((uint32_t)buf[25] << 8) | ((uint32_t)buf[26] << 16) | ((uint32_t)buf[27] << 24);
  }
  s_loaded = true;
}

bool Titan_isPaired(void) {
  return s_cfg.serial[0] != '\0';
}

const char* Titan_getSerial(void) {
  return s_cfg.serial;
}

const char* Titan_getPin(void) {
  return s_cfg.pin;
}

void Titan_setPaired(const char* serial, const char* pin) {
  if(serial == NULL || pin == NULL || strlen(serial) != GATE_SERIAL_LEN || strlen(pin) != GATE_PIN_LEN)
    return;
  memcpy(s_cfg.serial, serial, sizeof(s_cfg.serial));
  memcpy(s_cfg.pin, pin, sizeof(s_cfg.pin));
  _save();
}

void Titan_forget(void) {
  memset(s_cfg.serial, 0, sizeof(s_cfg.serial));
  memset(s_cfg.pin, 0, sizeof(s_cfg.pin));
  _save();
}

uint8_t Titan_getMagSize(void) {
  return s_cfg.magSize;
}

void Titan_setMagSize(uint8_t steps) {
  if(steps > TITAN_MAG_STEPS)
    return;
  s_cfg.magSize = steps;
  s_cfg.rounds = Titan_getCapacity();
  s_uncertain = false;
  _save();
}

uint16_t Titan_getCapacity(void) {
  return (uint16_t)(s_cfg.magSize * 10u);
}

uint8_t Titan_getAmmoAlert(void) {
  return s_cfg.ammoAlert;
}

void Titan_setAmmoAlert(uint8_t value) {
  if(value >= TITAN_AMMO_ALERT_COUNT)
    return;
  s_cfg.ammoAlert = value;
  _save();
}

uint8_t Titan_getBattery(void) {
  return s_cfg.battery;
}

void Titan_setBattery(uint8_t type) {
  if(type >= TITAN_BATT_COUNT)
    return;
  s_cfg.battery = type;
  s_low = false;
  _save();
}

const char* Titan_batteryName(uint8_t type) {
  return (type < TITAN_BATT_COUNT) ? kBatteries[type].name : "";
}

uint16_t Titan_batteryLimitMv(uint8_t type) {
  return (type < TITAN_BATT_COUNT) ? kBatteries[type].limitMv : 0;
}

// --- Open / close

void Titan_open(uint32_t now) {
  if(!s_loaded)
    Titan_load();
  s_openedAt = now;
  s_open = true;
  s_activeAlerts = 0;
  s_pendingAlerts = 0;
  s_haveInfo = false;
  s_low = false;
  // Shots while the app was closed are not counted: "~" until the next reload
  s_uncertain = (s_cfg.magSize > 0) || (s_cfg.sessionShots > 0);
}

void Titan_close(uint32_t now) {
  if(!s_open)
    return;
  s_cfg.sessionMs += now - s_openedAt;
  s_open = false;
  Titan_linkStop();
  s_scanning = false;
  _save();
}

// --- Search

bool Titan_scanStart(uint32_t now) {
  if(s_phase != PHASE__OFF)
    return false;
  s_foundCount = 0;
  s_scanning = true;
  s_scanStarted = GateBle_scan(TITAN_SCAN_MS);
  s_scanAskedAt = now;
  return true;
}

void Titan_scanStop(void) {
  if(s_scanning)
    GateBle_disconnect();
  s_scanning = false;
}

bool Titan_isScanning(void) {
  return s_scanning;
}

uint8_t Titan_getFoundCount(void) {
  return s_foundCount;
}

const titan_found_t* Titan_getFound(uint8_t index) {
  return (index < s_foundCount) ? &s_found[index] : NULL;
}

// --- Connection

void Titan_linkStart(const char* serial, const char* pin, uint32_t now) {
  if(serial == NULL || pin == NULL || strlen(serial) != GATE_SERIAL_LEN || strlen(pin) != GATE_PIN_LEN)
    return;
  if(s_scanning) {
    GateBle_disconnect();
    s_scanning = false;
  }
  if(s_phase != PHASE__OFF)
    GateBle_disconnect();
  memcpy(s_linkSerial, serial, sizeof(s_linkSerial));
  memcpy(s_linkPin, pin, sizeof(s_linkPin));
  s_phase = PHASE__WAIT;
  s_retryAt = now;
  s_wasReady = false;
  s_lost = false;
}

void Titan_linkStop(void) {
  if(s_phase != PHASE__OFF)
    GateBle_disconnect();
  s_phase = PHASE__OFF;
  s_lost = false;
}

uint8_t Titan_getLink(void) {
  switch(s_phase) {
    case PHASE__WAIT:
    case PHASE__FINDING:   return TITAN_LINK__SEARCHING;
    case PHASE__PIN:       return TITAN_LINK__PIN;
    case PHASE__READY:     return TITAN_LINK__READY;
    case PHASE__WRONG_PIN: return TITAN_LINK__WRONG_PIN;
    default:               return TITAN_LINK__OFF;
  }
}

bool Titan_wasReady(void) {
  return s_wasReady;
}

void Titan_update(uint32_t now) {
  gate_ble_event_t event;
  while(GateBle_getEvent(&event))
    _handleEvent(&event, now);

  // The search waits for the radio (it needs a moment after GateBle_start)
  if(s_scanning && !s_scanStarted) {
    s_scanStarted = GateBle_scan(TITAN_SCAN_MS);
    if(!s_scanStarted && (now - s_scanAskedAt) >= SCAN_START_LIMIT_MS)
      s_scanning = false;
  }

  _updateLink(now);
  _updateAlerts(now);
}

// --- Count

int32_t Titan_getRounds(void) {
  return (s_cfg.magSize > 0) ? (int32_t)s_cfg.rounds : (int32_t)s_cfg.sessionShots;
}

bool Titan_isUncertain(void) {
  return s_uncertain;
}

void Titan_reload(void) {
  s_cfg.rounds = Titan_getCapacity();
  if(s_cfg.reloads < 0xFFFF)
    s_cfg.reloads++;
  if(s_cfg.magSize > 0)
    s_uncertain = false;
}

void Titan_newSession(uint32_t now) {
  s_cfg.sessionShots = 0;
  s_cfg.reloads = 0;
  s_cfg.sessionMs = 0;
  s_cfg.rounds = Titan_getCapacity();
  s_openedAt = now;
  s_uncertain = false;
  _save();
}

uint32_t Titan_getSessionShots(void) {
  return s_cfg.sessionShots;
}

uint16_t Titan_getReloads(void) {
  return s_cfg.reloads;
}

uint32_t Titan_getSessionMs(uint32_t now) {
  return s_cfg.sessionMs + (s_open ? (now - s_openedAt) : 0);
}

bool Titan_getInfo(gate_info_t* info) {
  if(info != NULL && s_haveInfo)
    *info = s_info;
  return s_haveInfo;
}

// --- Alerts

uint8_t Titan_getActiveAlerts(void) {
  return s_activeAlerts;
}

uint8_t Titan_getAlert(void) {
  for(uint8_t bit = 0x01; bit != 0 && bit <= TITAN_ALERT__LOW_AMMO; bit = (uint8_t)(bit << 1)) {
    if(s_pendingAlerts & bit)
      return bit;
  }
  return 0;
}

void Titan_dismissAlert(void) {
  s_pendingAlerts &= (uint8_t)~Titan_getAlert();
}

/* > Local Function Definitions *******************************************************************/

static void _defaults(void) {
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.ammoAlert = 2;                      // 20 %
  s_cfg.battery = TITAN_BATT__2S_LIPO;
}

static void _save(void) {
  uint8_t buf[CFG_SIZE];
  memset(buf, 0, sizeof(buf));
  buf[0] = CFG_VERSION;
  memcpy(&buf[1], s_cfg.serial, GATE_SERIAL_LEN);
  memcpy(&buf[7], s_cfg.pin, GATE_PIN_LEN);
  buf[13] = s_cfg.magSize;
  buf[14] = s_cfg.ammoAlert;
  buf[15] = s_cfg.battery;
  buf[16] = (uint8_t)s_cfg.rounds;
  buf[17] = (uint8_t)(s_cfg.rounds >> 8);
  for(uint8_t i = 0; i < 4; i++) {
    buf[18 + i] = (uint8_t)(s_cfg.sessionShots >> (8 * i));
    buf[24 + i] = (uint8_t)(s_cfg.sessionMs >> (8 * i));
  }
  buf[22] = (uint8_t)s_cfg.reloads;
  buf[23] = (uint8_t)(s_cfg.reloads >> 8);

  nvs_handle_t handle;
  if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot open the flash");
    return;
  }
  if(nvs_set_blob(handle, NVS_KEY, buf, sizeof(buf)) != ESP_OK || nvs_commit(handle) != ESP_OK)
    ESP_LOGE(TAG, "Cannot save");
  nvs_close(handle);
}

static void _addFound(const char* serial, int8_t rssi) {
  for(uint8_t i = 0; i < s_foundCount; i++) {
    if(strcmp(s_found[i].serial, serial) == 0)
      return;
  }
  // Sorted, the strongest signal first. A full list keeps the strongest ones.
  uint8_t pos = s_foundCount;
  while(pos > 0 && s_found[pos - 1].rssi < rssi)
    pos--;
  if(pos >= TITAN_FOUND_MAX)
    return;
  const uint8_t last = (s_foundCount < TITAN_FOUND_MAX) ? s_foundCount : (TITAN_FOUND_MAX - 1);
  for(uint8_t i = last; i > pos; i--)
    s_found[i] = s_found[i - 1];
  memcpy(s_found[pos].serial, serial, sizeof(s_found[pos].serial));
  s_found[pos].rssi = rssi;
  if(s_foundCount < TITAN_FOUND_MAX)
    s_foundCount++;
}

static void _handleEvent(const gate_ble_event_t* event, uint32_t now) {
  switch(event->type) {
    case GATE_BLE_EV__SCAN_RESULT: {
      char serial[GATE_SERIAL_LEN + 1];
      if(s_scanning && Gate_parseName(event->name, serial))
        _addFound(serial, event->rssi);
    } break;

    case GATE_BLE_EV__SCAN_DONE:
      s_scanning = false;
      break;

    case GATE_BLE_EV__CONNECTED: {
      if(s_phase != PHASE__FINDING)
        break;
      uint8_t frame[GATE_FRAME_MAX];
      const uint8_t len = Gate_buildPin(frame, s_linkPin, s_linkSerial);
      if(len > 0 && GateBle_send(frame, len)) {
        s_phase = PHASE__PIN;
        s_pinSentAt = now;
      } else {
        GateBle_disconnect();
        s_phase = PHASE__WAIT;
        s_retryAt = now + RETRY_MS;
      }
    } break;

    case GATE_BLE_EV__CONNECT_FAILED:
      if(s_phase == PHASE__FINDING || s_phase == PHASE__PIN) {
        s_phase = PHASE__WAIT;
        s_retryAt = now + RETRY_MS;
      }
      break;

    case GATE_BLE_EV__DISCONNECTED:
      if(s_phase == PHASE__READY)
        _lost(now);
      if(s_phase == PHASE__READY || s_phase == PHASE__PIN || s_phase == PHASE__FINDING) {
        s_phase = PHASE__WAIT;
        s_retryAt = now + RETRY_MS;
      }
      break;

    case GATE_BLE_EV__ANSWER: {
      uint8_t cmd = 0;
      const gate_answer_t answer = Gate_parseAnswer(event->data, event->len, &cmd);
      if(s_phase != PHASE__PIN || cmd != GATE_CMD__PIN)
        break;
      if(answer == GATE_ANSWER__OK) {
        s_phase = PHASE__READY;
        s_wasReady = true;
        s_lost = false;
        s_haveInfo = false;
        s_lastReadAt = now;
        GateBle_readInfo();
      } else if(answer == GATE_ANSWER__ERROR) {
        GateBle_disconnect();
        s_phase = PHASE__WRONG_PIN;
      }
    } break;

    case GATE_BLE_EV__INFO:
    case GATE_BLE_EV__INFO_READ: {
      gate_info_t info;
      if(s_phase == PHASE__READY && Gate_parseInfo(event->data, event->len, &info))
        _applyInfo(&info, event->type == GATE_BLE_EV__INFO, now);
    } break;

    default: break;
  }
}

static void _applyInfo(const gate_info_t* info, bool notification, uint32_t now) {
  // A reload on the ETU side, or a new magazine in the mag sensor. Before the shots: the shots
  // of this message are from the new mag.
  if(s_haveInfo) {
    const bool bbReset = !(s_info.flags & GATE_FLAG__BB_RESET) && (info->flags & GATE_FLAG__BB_RESET);
    const bool newMag = (info->flags & GATE_FLAG__MAG_SENSOR) && (s_info.flags & GATE_FLAG__NO_MAG) &&
                        !(info->flags & GATE_FLAG__NO_MAG);
    if(bbReset || newMag)
      Titan_reload();
    if(info->selector != s_info.selector || info->trigger != s_info.trigger)
      App_updateActivity(now);
  }

  // Only a notification has new shots: a read gives the value of the last notification again
  if(notification && info->fired > 0) {
    s_cfg.sessionShots += info->fired;
    if(s_cfg.magSize > 0)
      s_cfg.rounds = (info->fired >= s_cfg.rounds) ? 0 : (uint16_t)(s_cfg.rounds - info->fired);
    App_updateActivity(now);
  }

  // The voltage: under the limit since when
  const uint16_t limit = Titan_batteryLimitMv(s_cfg.battery);
  if(info->voltageMv != GATE_VOLTAGE_UNKNOWN) {
    if(limit > 0 && info->voltageMv < limit) {
      if(!s_low) {
        s_low = true;
        s_lowSince = now;
      }
    } else {
      s_low = false;
    }
  }

  s_info = *info;
  s_haveInfo = true;
}

static void _lost(uint32_t now) {
  s_lost = true;
  s_lostAt = now;
  s_uncertain = true;
  s_haveInfo = false;
  s_low = false;
}

static void _updateLink(uint32_t now) {
  switch(s_phase) {
    case PHASE__WAIT:
      if((int32_t)(now - s_retryAt) >= 0) {
        if(GateBle_connect(s_linkSerial, CONNECT_TRY_MS))
          s_phase = PHASE__FINDING;
        else
          s_retryAt = now + START_RETRY_MS;   // The radio is not ready yet
      }
      break;

    case PHASE__PIN:
      if((now - s_pinSentAt) >= TITAN_PIN_ANSWER_MS) {
        GateBle_disconnect();
        s_phase = PHASE__WAIT;
        s_retryAt = now + RETRY_MS;
      }
      break;

    case PHASE__READY:
      if((now - s_lastReadAt) >= TITAN_INFO_READ_MS) {
        s_lastReadAt = now;
        GateBle_readInfo();
      }
      break;

    default: break;
  }
}

static void _updateAlerts(uint32_t now) {
  uint8_t active = 0;
  const bool ready = (s_phase == PHASE__READY);

  if(s_wasReady && !ready && s_lost && (now - s_lostAt) >= TITAN_LINK_LOST_MS)
    active |= TITAN_ALERT__LINK_LOST;
  if(ready && s_low && (now - s_lowSince) >= TITAN_LOW_BATT_MS)
    active |= TITAN_ALERT__LOW_BATT;
  if(ready && s_haveInfo && s_info.fireLocked)
    active |= TITAN_ALERT__FIRE_LOCKED;

  const uint16_t capacity = Titan_getCapacity();
  if(capacity > 0) {
    if(s_cfg.rounds == 0) {
      active |= TITAN_ALERT__MAG_EMPTY;
    } else {
      const uint32_t limit = (uint32_t)capacity * kAmmoAlertPercent[s_cfg.ammoAlert] / 100u;
      if(limit > 0 && s_cfg.rounds <= limit)
        active |= TITAN_ALERT__LOW_AMMO;
    }
  }

  // An alert comes when its problem starts. It stays until a button closes it or the problem stops.
  const uint8_t rising = (uint8_t)(active & ~s_activeAlerts);
  s_pendingAlerts = (uint8_t)((s_pendingAlerts | rising) & active);
  s_activeAlerts = active;
}
