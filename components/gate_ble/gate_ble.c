/***************************************************************************************************
 * @file    : gate_ble.c
 * @brief   : Bluetooth to a GATE ETU, with NimBLE in the central role (see gate_ble.h).
 *
 * The sequence for a connection:
 *   1. Search (active scan, so the scan response with the name comes too). The name of an ETU is
 *      "TTN2B BT RD5ZEI": the model, " BT ", the serial number.
 *   2. Connect to the ETU with the serial number.
 *   3. Find the GATE service, its characteristics "Commands" and "INFO_0_API", and their CCCD.
 *   4. Switch the notifications on for both. Then GATE_BLE_EV__CONNECTED.
 * The PIN (main/titan.c) must come in 30 s after the connection, or the ETU disconnects.
 *
 * Callbacks run in the NimBLE host task. They only put events into the queue.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "gate_ble.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

/* > Defines **************************************************************************************/
#define TAG               "GateBle"
#define QUEUE_LENGTH      24
#define CONNECT_TIMEOUT_MS 10000
#define SEEN_MAX          8          // Different ETUs in one search
#define CHR_MAX           12         // Characteristics in the GATE service
#define SERIAL_LEN        6
#define NAME_LEN          15         // "TTN2B BT RD5ZEI"

/* > Type Definitions *****************************************************************************/
typedef enum state_e {
  ST__OFF = 0,
  ST__IDLE,
  ST__SCAN,         // GateBle_scan
  ST__FIND,         // GateBle_connect: searching for the serial number
  ST__CONNECTING,
  ST__DISCOVER,     // Connected, finding the characteristics
  ST__READY,
} state_t;

/* > Local Function Declarations ******************************************************************/
static void _onReset(int reason);
static void _onSync(void);
static void _hostTask(void* param);
static int  _gapEvent(struct ble_gap_event* event, void* arg);
static int  _svcCb(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void* arg);
static int  _chrCb(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg);
static int  _dscCb(uint16_t conn, const struct ble_gatt_error* error, uint16_t chrValHandle,
                   const struct ble_gatt_dsc* dsc, void* arg);
static int  _cccdWriteCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg);
static int  _readCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg);
static int  _writeCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg);
static void _discoverCccd(uint8_t step);
static void _failConnection(void);
static void _push(uint8_t type, const uint8_t* data, uint16_t len);
static bool _nameOf(const struct ble_gap_disc_desc* disc, char* name);
static uint16_t _chrEnd(uint16_t valHandle);

/* > Local Variables ******************************************************************************/
// a64379fd-cecd-4497-bb36-9e1a4674a973 (low byte first)
static const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
  0x73, 0xa9, 0x74, 0x46, 0x1a, 0x9e, 0x36, 0xbb, 0x97, 0x44, 0xcd, 0xce, 0xfd, 0x79, 0x43, 0xa6);
// 31a399e4-0e34-46f9-8ee2-bbee2f09ecf8: Commands
static const ble_uuid128_t kCommandsUuid = BLE_UUID128_INIT(
  0xf8, 0xec, 0x09, 0x2f, 0xee, 0xbb, 0xe2, 0x8e, 0xf9, 0x46, 0x34, 0x0e, 0xe4, 0x99, 0xa3, 0x31);
// 3adc82fc-a98b-404f-9ed1-a0983429530f: INFO_0_API
static const ble_uuid128_t kInfoUuid = BLE_UUID128_INIT(
  0x0f, 0x53, 0x29, 0x34, 0x98, 0xa0, 0xd1, 0x9e, 0x4f, 0x40, 0x8b, 0xa9, 0xfc, 0x82, 0xdc, 0x3a);

static QueueHandle_t     s_queue = NULL;
static volatile uint8_t  s_state = ST__OFF;
static volatile bool     s_synced = false;
static uint8_t           s_ownAddrType = 0;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static char              s_target[SERIAL_LEN + 1];

static ble_addr_t        s_seen[SEEN_MAX];
static uint8_t           s_seenCount = 0;

static uint16_t          s_svcStart, s_svcEnd;
static uint16_t          s_cmdVal, s_infoVal, s_cmdCccd, s_infoCccd;
static uint16_t          s_chrDefs[CHR_MAX];
static uint8_t           s_chrCount = 0;

/* > Function Definitions *************************************************************************/

bool GateBle_start(void) {
  if(s_state != ST__OFF)
    return false;

  s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(gate_ble_event_t));
  if(s_queue == NULL)
    return false;

  if(nimble_port_init() != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed");
    vQueueDelete(s_queue);
    s_queue = NULL;
    return false;
  }

  ble_hs_cfg.reset_cb = _onReset;
  ble_hs_cfg.sync_cb = _onSync;
  ble_hs_cfg.gatts_register_cb = NULL;
  ble_hs_cfg.store_status_cb = NULL;

  s_synced = false;
  s_conn = BLE_HS_CONN_HANDLE_NONE;
  s_state = ST__IDLE;
  nimble_port_freertos_init(_hostTask);
  return true;
}

void GateBle_stop(void) {
  if(s_state == ST__OFF)
    return;

  const uint8_t state = s_state;
  s_state = ST__OFF;   // The callbacks put no more events into the queue
  if(state == ST__SCAN || state == ST__FIND)
    ble_gap_disc_cancel();
  if(s_conn != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
  else if(state == ST__CONNECTING)
    ble_gap_conn_cancel();

  // As components/app_ble/app_ble.c: the host task ends, then the controller is released
  if(nimble_port_stop() == 0)
    nimble_port_deinit();

  s_conn = BLE_HS_CONN_HANDLE_NONE;
  s_synced = false;
  if(s_queue != NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
  }
}

bool GateBle_isOn(void) {
  return s_state != ST__OFF;
}

bool GateBle_scan(uint32_t ms) {
  if(s_state != ST__IDLE || !s_synced)
    return false;

  struct ble_gap_disc_params params;
  memset(&params, 0, sizeof(params));
  params.passive = 0;             // Active: the scan response has the name
  params.filter_duplicates = 0;   // The controller filter can drop the scan response; _gapEvent filters

  s_seenCount = 0;
  s_state = ST__SCAN;
  if(ble_gap_disc(s_ownAddrType, (int32_t)ms, &params, _gapEvent, NULL) != 0) {
    s_state = ST__IDLE;
    return false;
  }
  return true;
}

bool GateBle_connect(const char* serial, uint32_t ms) {
  if(s_state != ST__IDLE || !s_synced || serial == NULL || strlen(serial) != SERIAL_LEN)
    return false;

  struct ble_gap_disc_params params;
  memset(&params, 0, sizeof(params));
  params.passive = 0;
  params.filter_duplicates = 0;

  memcpy(s_target, serial, SERIAL_LEN + 1);
  s_state = ST__FIND;
  if(ble_gap_disc(s_ownAddrType, (int32_t)ms, &params, _gapEvent, NULL) != 0) {
    s_state = ST__IDLE;
    return false;
  }
  return true;
}

void GateBle_disconnect(void) {
  const uint8_t state = s_state;
  if(state == ST__OFF || state == ST__IDLE)
    return;

  s_state = ST__IDLE;   // No events for this connection any more
  if(state == ST__SCAN || state == ST__FIND)
    ble_gap_disc_cancel();
  else if(state == ST__CONNECTING)
    ble_gap_conn_cancel();
  if(s_conn != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}

bool GateBle_isConnected(void) {
  return s_state == ST__READY;
}

bool GateBle_send(const uint8_t* frame, uint8_t len) {
  if(s_state != ST__READY || frame == NULL || len == 0 || len > GATE_BLE_DATA_MAX)
    return false;
  return ble_gattc_write_flat(s_conn, s_cmdVal, frame, len, _writeCb, NULL) == 0;
}

bool GateBle_readInfo(void) {
  if(s_state != ST__READY)
    return false;
  return ble_gattc_read(s_conn, s_infoVal, _readCb, NULL) == 0;
}

bool GateBle_getEvent(gate_ble_event_t* event) {
  if(s_queue == NULL || event == NULL)
    return false;
  return xQueueReceive(s_queue, event, 0) == pdTRUE;
}

/* > Local Function Definitions *******************************************************************/

static void _onReset(int reason) {
  ESP_LOGW(TAG, "Host reset, reason %d", reason);
  s_synced = false;
}

static void _onSync(void) {
  if(ble_hs_util_ensure_addr(0) != 0)
    return;
  if(ble_hs_id_infer_auto(0, &s_ownAddrType) != 0)
    return;
  s_synced = true;
}

static void _hostTask(void* param) {
  (void)param;
  nimble_port_run();   // Returns when nimble_port_stop() runs
  nimble_port_freertos_deinit();
}

static void _push(uint8_t type, const uint8_t* data, uint16_t len) {
  if(s_queue == NULL || s_state == ST__OFF)
    return;
  gate_ble_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  if(data != NULL && len > 0) {
    event.len = (uint8_t)((len > GATE_BLE_DATA_MAX) ? GATE_BLE_DATA_MAX : len);
    memcpy(event.data, data, event.len);
  }
  xQueueSend(s_queue, &event, 0);
}

// The name in the advertising data or the scan response, when it has the form of a GATE name
static bool _nameOf(const struct ble_gap_disc_desc* disc, char* name) {
  struct ble_hs_adv_fields fields;
  if(ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0)
    return false;
  if(fields.name == NULL || fields.name_len != NAME_LEN)
    return false;
  memcpy(name, fields.name, NAME_LEN);
  name[NAME_LEN] = '\0';
  return memcmp(&name[5], " BT ", 4) == 0;
}

static int _gapEvent(struct ble_gap_event* event, void* arg) {
  (void)arg;

  switch(event->type) {
    case BLE_GAP_EVENT_DISC: {
      char name[GATE_BLE_NAME_MAX + 1];
      if(!_nameOf(&event->disc, name))
        return 0;

      if(s_state == ST__SCAN) {
        for(uint8_t i = 0; i < s_seenCount; i++) {
          if(ble_addr_cmp(&s_seen[i], &event->disc.addr) == 0)
            return 0;
        }
        if(s_seenCount < SEEN_MAX)
          s_seen[s_seenCount++] = event->disc.addr;

        if(s_queue != NULL) {
          gate_ble_event_t result;
          memset(&result, 0, sizeof(result));
          result.type = GATE_BLE_EV__SCAN_RESULT;
          result.rssi = event->disc.rssi;
          memcpy(result.name, name, sizeof(result.name));
          xQueueSend(s_queue, &result, 0);
        }
      } else if(s_state == ST__FIND && memcmp(&name[NAME_LEN - SERIAL_LEN], s_target, SERIAL_LEN) == 0) {
        ble_gap_disc_cancel();
        s_state = ST__CONNECTING;
        if(ble_gap_connect(s_ownAddrType, &event->disc.addr, CONNECT_TIMEOUT_MS, NULL, _gapEvent, NULL) != 0) {
          s_state = ST__IDLE;
          _push(GATE_BLE_EV__CONNECT_FAILED, NULL, 0);
        }
      }
    } return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE: {
      if(s_state == ST__SCAN) {
        s_state = ST__IDLE;
        _push(GATE_BLE_EV__SCAN_DONE, NULL, 0);
      } else if(s_state == ST__FIND) {
        s_state = ST__IDLE;
        _push(GATE_BLE_EV__CONNECT_FAILED, NULL, 0);
      }
    } return 0;

    case BLE_GAP_EVENT_CONNECT: {
      if(event->connect.status != 0) {
        if(s_state == ST__CONNECTING) {
          s_state = ST__IDLE;
          _push(GATE_BLE_EV__CONNECT_FAILED, NULL, 0);
        }
        return 0;
      }
      s_conn = event->connect.conn_handle;
      if(s_state != ST__CONNECTING) {
        // Stopped while it connected
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
      }
      s_state = ST__DISCOVER;
      s_svcStart = s_svcEnd = 0;
      s_cmdVal = s_infoVal = s_cmdCccd = s_infoCccd = 0;
      s_chrCount = 0;
      if(ble_gattc_disc_svc_by_uuid(s_conn, &kServiceUuid.u, _svcCb, NULL) != 0)
        _failConnection();
    } return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
      // A late event of an old connection (GateBle_disconnect, then a new connection) changes nothing
      if(event->disconnect.conn.conn_handle != s_conn)
        return 0;
      const uint8_t state = s_state;
      s_conn = BLE_HS_CONN_HANDLE_NONE;
      if(state == ST__READY) {
        s_state = ST__IDLE;
        _push(GATE_BLE_EV__DISCONNECTED, NULL, 0);
      } else if(state == ST__DISCOVER || state == ST__CONNECTING) {
        s_state = ST__IDLE;
        _push(GATE_BLE_EV__CONNECT_FAILED, NULL, 0);
      }
    } return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
      if(s_state != ST__READY)
        return 0;
      uint8_t data[GATE_BLE_DATA_MAX];
      uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
      if(len > sizeof(data))
        len = sizeof(data);
      if(os_mbuf_copydata(event->notify_rx.om, 0, len, data) != 0)
        return 0;
      if(event->notify_rx.attr_handle == s_infoVal)
        _push(GATE_BLE_EV__INFO, data, len);
      else if(event->notify_rx.attr_handle == s_cmdVal)
        _push(GATE_BLE_EV__ANSWER, data, len);
    } return 0;

    default:
      return 0;
  }
}

static void _failConnection(void) {
  // The DISCONNECT event then gives CONNECT_FAILED
  if(s_conn != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}

static int _svcCb(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void* arg) {
  (void)arg;
  if(s_state != ST__DISCOVER)
    return 0;

  if(error->status == 0 && service != NULL) {
    s_svcStart = service->start_handle;
    s_svcEnd = service->end_handle;
  } else if(error->status == BLE_HS_EDONE) {
    if(s_svcStart == 0 || ble_gattc_disc_all_chrs(conn, s_svcStart, s_svcEnd, _chrCb, NULL) != 0)
      _failConnection();
  } else {
    _failConnection();
  }
  return 0;
}

static int _chrCb(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg) {
  (void)conn;
  (void)arg;
  if(s_state != ST__DISCOVER)
    return 0;

  if(error->status == 0 && chr != NULL) {
    if(s_chrCount < CHR_MAX)
      s_chrDefs[s_chrCount++] = chr->def_handle;
    if(ble_uuid_cmp(&chr->uuid.u, &kCommandsUuid.u) == 0)
      s_cmdVal = chr->val_handle;
    else if(ble_uuid_cmp(&chr->uuid.u, &kInfoUuid.u) == 0)
      s_infoVal = chr->val_handle;
  } else if(error->status == BLE_HS_EDONE) {
    if(s_cmdVal == 0 || s_infoVal == 0)
      _failConnection();
    else
      _discoverCccd(0);
  } else {
    _failConnection();
  }
  return 0;
}

// The last handle of a characteristic: the handle before the next characteristic, or the end
// of the service
static uint16_t _chrEnd(uint16_t valHandle) {
  uint16_t end = s_svcEnd;
  for(uint8_t i = 0; i < s_chrCount; i++) {
    if(s_chrDefs[i] > valHandle && (uint16_t)(s_chrDefs[i] - 1) < end)
      end = (uint16_t)(s_chrDefs[i] - 1);
  }
  return end;
}

// Step 0: the CCCD of Commands, step 1: the CCCD of INFO_0_API, step 2: switch the notifications on
static void _discoverCccd(uint8_t step) {
  if(step < 2) {
    const uint16_t val = (step == 0) ? s_cmdVal : s_infoVal;
    const uint16_t end = _chrEnd(val);
    if(end <= val || ble_gattc_disc_all_dscs(s_conn, val, end, _dscCb, (void*)(uintptr_t)step) != 0)
      _failConnection();
    return;
  }

  if(s_cmdCccd == 0 || s_infoCccd == 0) {
    _failConnection();
    return;
  }
  static const uint8_t kOn[2] = { 0x01, 0x00 };
  if(ble_gattc_write_flat(s_conn, s_cmdCccd, kOn, sizeof(kOn), _cccdWriteCb, (void*)(uintptr_t)0) != 0)
    _failConnection();
}

static int _dscCb(uint16_t conn, const struct ble_gatt_error* error, uint16_t chrValHandle,
                  const struct ble_gatt_dsc* dsc, void* arg) {
  (void)conn;
  (void)chrValHandle;
  const uint8_t step = (uint8_t)(uintptr_t)arg;
  if(s_state != ST__DISCOVER)
    return 0;

  if(error->status == 0 && dsc != NULL) {
    if(ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
      if(step == 0)
        s_cmdCccd = dsc->handle;
      else
        s_infoCccd = dsc->handle;
    }
  } else if(error->status == BLE_HS_EDONE) {
    _discoverCccd((uint8_t)(step + 1));
  } else {
    _failConnection();
  }
  return 0;
}

static int _cccdWriteCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg) {
  (void)attr;
  const uint8_t step = (uint8_t)(uintptr_t)arg;
  if(s_state != ST__DISCOVER)
    return 0;

  if(error->status != 0) {
    _failConnection();
  } else if(step == 0) {
    static const uint8_t kOn[2] = { 0x01, 0x00 };
    if(ble_gattc_write_flat(conn, s_infoCccd, kOn, sizeof(kOn), _cccdWriteCb, (void*)(uintptr_t)1) != 0)
      _failConnection();
  } else {
    s_state = ST__READY;
    _push(GATE_BLE_EV__CONNECTED, NULL, 0);
  }
  return 0;
}

static int _readCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg) {
  (void)conn;
  (void)arg;
  if(s_state != ST__READY || error->status != 0 || attr == NULL || attr->om == NULL)
    return 0;
  uint8_t data[GATE_BLE_DATA_MAX];
  uint16_t len = OS_MBUF_PKTLEN(attr->om);
  if(len > sizeof(data))
    len = sizeof(data);
  if(os_mbuf_copydata(attr->om, 0, len, data) == 0)
    _push(GATE_BLE_EV__INFO_READ, data, len);
  return 0;
}

static int _writeCb(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if(error->status != 0)
    ESP_LOGW(TAG, "Write failed: %d", error->status);
  return 0;
}
