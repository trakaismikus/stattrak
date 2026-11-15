/***************************************************************************************************
 * @file    : host_gate.c
 * @brief   : Stand-in for components/gate_ble/gate_ble.c: fake GATE TITAN II Bluetooth units.
 *
 * A test adds a Titan (serial number, PIN, signal), moves it out of range, and lets it send
 * INFO_0_API. The fake ETU does what the GATE document says: it answers the PIN with "OK" or
 * "ER", it disconnects when no PIN comes in 30 s, and it sends INFO only after a correct PIN.
 * The delays are short, fixed numbers, so the tests are repeatable.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"
#include "gate_ble.h"
#include "gate.h"

#include <stdio.h>
#include <string.h>

/* > Defines **************************************************************************************/
#define ETU_MAX           4
#define QUEUE_MAX         32
#define SYNC_MS           50      // The radio is ready this long after GateBle_start
#define FOUND_MS          300     // A search finds a Titan that is in range after this time
#define CONNECT_MS        150     // Found -> connected (with the discovery)
#define ANSWER_MS         20
#define PIN_LIMIT_MS      30000   // The ETU disconnects without a correct PIN

/* > Type Definitions *****************************************************************************/
typedef struct etu_s {
  bool    used;
  bool    inRange;
  char    serial[GATE_SERIAL_LEN + 1];
  char    pin[GATE_PIN_LEN + 1];
  int8_t  rssi;
  uint8_t info[GATE_BLE_DATA_MAX];
  uint8_t infoLen;
} etu_t;

typedef enum mode_e { MODE__OFF, MODE__IDLE, MODE__SCAN, MODE__FIND, MODE__CONNECTED } mode_t_;

typedef struct pending_s {
  uint64_t at;
  gate_ble_event_t event;
} pending_t;

/* > Local Variables ******************************************************************************/
static etu_t     s_etus[ETU_MAX];
static uint8_t   s_mode = MODE__OFF;
static uint64_t  s_startedAt = 0;
static uint64_t  s_modeEnd = 0;          // SCAN / FIND: the end of the search
static uint64_t  s_modeAt = 0;           // SCAN / FIND: the start
static bool      s_reported[ETU_MAX];    // SCAN: reported already
static char      s_target[GATE_SERIAL_LEN + 1];
static int8_t    s_conn = -1;            // Index of the connected ETU
static bool      s_authorized = false;
static uint64_t  s_connectedAt = 0;

static pending_t s_queue[QUEUE_MAX];
static uint8_t   s_queueCount = 0;

static uint32_t  s_startCount = 0;
static uint32_t  s_overlapCount = 0;
static uint32_t  s_sendCount = 0;
static uint8_t   s_lastCommand = 0;
static uint32_t  s_otherCount = 0;

/* > Local Functions ******************************************************************************/

static int8_t _find(const char* serial) {
  for(int8_t i = 0; i < ETU_MAX; i++) {
    if(s_etus[i].used && strcmp(s_etus[i].serial, serial) == 0)
      return i;
  }
  return -1;
}

static void _queue(uint64_t at, uint8_t type, const uint8_t* data, uint8_t len) {
  if(s_queueCount >= QUEUE_MAX)
    return;
  pending_t* p = &s_queue[s_queueCount++];
  memset(p, 0, sizeof(*p));
  p->at = at;
  p->event.type = type;
  if(data != NULL && len > 0) {
    p->event.len = (len > GATE_BLE_DATA_MAX) ? GATE_BLE_DATA_MAX : len;
    memcpy(p->event.data, data, p->event.len);
  }
}

static void _dropConnection(bool report) {
  if(s_mode == MODE__CONNECTED && report)
    _queue(Host_nowMs(), GATE_BLE_EV__DISCONNECTED, NULL, 0);
  s_conn = -1;
  s_authorized = false;
  if(s_mode != MODE__OFF)
    s_mode = MODE__IDLE;
}

// Moves the fake radio forward to the current time
static void _tick(void) {
  const uint64_t now = Host_nowMs();

  if(s_mode == MODE__SCAN) {
    for(uint8_t i = 0; i < ETU_MAX; i++) {
      if(s_etus[i].used && s_etus[i].inRange && !s_reported[i] && now >= s_modeAt + FOUND_MS) {
        s_reported[i] = true;
        if(s_queueCount < QUEUE_MAX) {
          pending_t* p = &s_queue[s_queueCount++];
          memset(p, 0, sizeof(*p));
          p->at = now;
          p->event.type = GATE_BLE_EV__SCAN_RESULT;
          p->event.rssi = s_etus[i].rssi;
          snprintf(p->event.name, sizeof(p->event.name), "TTN2B BT %s", s_etus[i].serial);
        }
      }
    }
    if(now >= s_modeEnd) {
      s_mode = MODE__IDLE;
      _queue(now, GATE_BLE_EV__SCAN_DONE, NULL, 0);
    }
  } else if(s_mode == MODE__FIND) {
    const int8_t i = _find(s_target);
    if(i >= 0 && s_etus[i].inRange && now >= s_modeAt + FOUND_MS + CONNECT_MS) {
      s_mode = MODE__CONNECTED;
      s_conn = i;
      s_authorized = false;
      s_connectedAt = now;
      _queue(now, GATE_BLE_EV__CONNECTED, NULL, 0);
    } else if(now >= s_modeEnd) {
      s_mode = MODE__IDLE;
      _queue(now, GATE_BLE_EV__CONNECT_FAILED, NULL, 0);
    }
  } else if(s_mode == MODE__CONNECTED) {
    if(!s_authorized && now >= s_connectedAt + PIN_LIMIT_MS)
      _dropConnection(true);
  }
}

/* > Stand-in API (components/gate_ble/gate_ble.h) ************************************************/

bool GateBle_start(void) {
  if(s_mode != MODE__OFF)
    return false;
  if(HostOta_isActive())
    s_overlapCount++;
  s_startCount++;
  s_mode = MODE__IDLE;
  s_startedAt = Host_nowMs();
  s_queueCount = 0;
  return true;
}

void GateBle_stop(void) {
  s_mode = MODE__OFF;
  s_conn = -1;
  s_authorized = false;
  s_queueCount = 0;
}

bool GateBle_isOn(void) {
  return s_mode != MODE__OFF;
}

static bool _ready(void) {
  return s_mode == MODE__IDLE && Host_nowMs() >= s_startedAt + SYNC_MS;
}

bool GateBle_scan(uint32_t ms) {
  _tick();
  if(!_ready())
    return false;
  s_mode = MODE__SCAN;
  s_modeAt = Host_nowMs();
  s_modeEnd = s_modeAt + ms;
  memset(s_reported, 0, sizeof(s_reported));
  return true;
}

bool GateBle_connect(const char* serial, uint32_t ms) {
  _tick();
  if(!_ready() || serial == NULL || strlen(serial) != GATE_SERIAL_LEN)
    return false;
  memcpy(s_target, serial, sizeof(s_target));
  s_mode = MODE__FIND;
  s_modeAt = Host_nowMs();
  s_modeEnd = s_modeAt + ms;
  return true;
}

void GateBle_disconnect(void) {
  if(s_mode == MODE__OFF)
    return;
  s_conn = -1;
  s_authorized = false;
  s_mode = MODE__IDLE;
  // Events of the old connection are not delivered
  s_queueCount = 0;
}

bool GateBle_isConnected(void) {
  return s_mode == MODE__CONNECTED;
}

bool GateBle_send(const uint8_t* frame, uint8_t len) {
  _tick();
  if(s_mode != MODE__CONNECTED || s_conn < 0 || frame == NULL || len < 4 || len > GATE_BLE_DATA_MAX)
    return false;
  s_sendCount++;
  s_lastCommand = frame[2];

  // The ETU ignores a frame with a wrong structure (GATE document: no answer)
  if(frame[0] != GATE_ID__TO_ETU || frame[1] != len || Gate_crc8(frame, (uint8_t)(len - 1)) != frame[len - 1])
    return true;

  if(frame[2] != GATE_CMD__PIN) {
    s_otherCount++;
    return true;
  }

  const etu_t* etu = &s_etus[s_conn];
  const bool good = (len == 16) && memcmp(&frame[3], etu->pin, GATE_PIN_LEN) == 0 &&
                    memcmp(&frame[3 + GATE_PIN_LEN], etu->serial, GATE_SERIAL_LEN) == 0;
  uint8_t answer[6] = { GATE_ID__FROM_ETU, 6, GATE_CMD__PIN, good ? 'O' : 'E', good ? 'K' : 'R', 0 };
  answer[5] = Gate_crc8(answer, 5);
  if(good)
    s_authorized = true;
  _queue(Host_nowMs() + ANSWER_MS, GATE_BLE_EV__ANSWER, answer, sizeof(answer));
  return true;
}

bool GateBle_readInfo(void) {
  _tick();
  if(s_mode != MODE__CONNECTED || s_conn < 0 || !s_authorized)
    return false;
  const etu_t* etu = &s_etus[s_conn];
  _queue(Host_nowMs() + ANSWER_MS, GATE_BLE_EV__INFO_READ, etu->info, etu->infoLen);
  return true;
}

bool GateBle_getEvent(gate_ble_event_t* event) {
  _tick();
  if(event == NULL || s_mode == MODE__OFF)
    return false;
  const uint64_t now = Host_nowMs();
  for(uint8_t i = 0; i < s_queueCount; i++) {
    if(s_queue[i].at <= now) {
      *event = s_queue[i].event;
      memmove(&s_queue[i], &s_queue[i + 1], (size_t)(s_queueCount - i - 1) * sizeof(pending_t));
      s_queueCount--;
      return true;
    }
  }
  return false;
}

/* > Test API (host.h) ****************************************************************************/

bool HostGate_add(const char* serial, const char* pin, int8_t rssi) {
  if(serial == NULL || pin == NULL || strlen(serial) != GATE_SERIAL_LEN || strlen(pin) != GATE_PIN_LEN)
    return false;
  int8_t i = _find(serial);
  if(i < 0) {
    for(i = 0; i < ETU_MAX && s_etus[i].used; i++) {}
    if(i >= ETU_MAX)
      return false;
  }
  etu_t* etu = &s_etus[i];
  memset(etu, 0, sizeof(*etu));
  etu->used = true;
  etu->inRange = true;
  memcpy(etu->serial, serial, sizeof(etu->serial));
  memcpy(etu->pin, pin, sizeof(etu->pin));
  etu->rssi = rssi;
  // Start state: unlocked, no sensors, SEMI, trigger released, 0 shots, 8.0 V
  const uint8_t info[8] = { 0, 0, GATE_SELECTOR__SEMI, 0, 0, 0, 0x40, 0x1F };
  memcpy(etu->info, info, sizeof(info));
  etu->infoLen = sizeof(info);
  return true;
}

bool HostGate_setInRange(const char* serial, bool inRange) {
  const int8_t i = _find(serial);
  if(i < 0)
    return false;
  s_etus[i].inRange = inRange;
  if(!inRange && s_conn == i)
    _dropConnection(true);
  return true;
}

bool HostGate_sendInfo(const uint8_t* data, uint8_t len) {
  _tick();
  if(data == NULL || len < GATE_INFO_LEN || len > GATE_BLE_DATA_MAX)
    return false;
  if(s_mode != MODE__CONNECTED || s_conn < 0 || !s_authorized)
    return false;
  etu_t* etu = &s_etus[s_conn];
  memcpy(etu->info, data, len);
  etu->infoLen = len;
  _queue(Host_nowMs(), GATE_BLE_EV__INFO, data, len);
  return true;
}

bool HostGate_isOn(void)            { return s_mode != MODE__OFF; }
bool HostGate_isConnected(void)     { _tick(); return s_mode == MODE__CONNECTED; }
bool HostGate_isAuthorized(void)    { return s_mode == MODE__CONNECTED && s_authorized; }
uint32_t HostGate_getStartCount(void)   { return s_startCount; }
uint32_t HostGate_getOverlapCount(void) { return s_overlapCount; }
void     HostGate_noteOverlap(void)     { s_overlapCount++; }
uint32_t HostGate_getSendCount(void)    { return s_sendCount; }
uint8_t  HostGate_getLastCommand(void)  { return s_lastCommand; }
uint32_t HostGate_getOtherCommandCount(void) { return s_otherCount; }

void HostGate_reset(void) {
  memset(s_etus, 0, sizeof(s_etus));
  GateBle_stop();
}
