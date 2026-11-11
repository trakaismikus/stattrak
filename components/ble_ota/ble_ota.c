
/* > Includes *************************************************************************************/
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"

#include "ble_ota.h"

/* > Definitions **********************************************************************************/
#define GATT_SVR_SVC_ALERT_UUID             0x1811
#define BLE_OTA_SERVICE_UUID                0x8018

#define RECV_FW_UUID                        0x8020
#define OTA_BAR_UUID                        0x8021
#define COMMAND_UUID                        0x8022
#define CUSTOMER_UUID                       0x8023

/* > Enumerations **********************************************************************************/
enum cmd_id_e {
  CMD_ID__START = 0x0001,
  CMD_ID__STOP  = 0x0002,
  CMD_ID__ACK   = 0x0003,
};

enum cmd_ack_e {
  CMD_ACK__ACCEPT = 0x0000,
  CMD_ACK__REJECT = 0x0001,
};

enum ack_status_e {
  ACK_STATUS__SUCCESS         = 0x0000,
  ACK_STATUS__CRC_ERR         = 0x0001,
  ACK_STATUS__SECTOR_IDX_ERR  = 0x0002,
  ACK_STATUS__PAYLOAD_LEN_ERR = 0x0003,
};


/* > Local Function Declarations ******************************************************************/

static int _otaSvcAccess(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt,
                void *arg);

static int _writeCommand(struct os_mbuf *om);
static int _writeFw(struct os_mbuf *om);
static int _writeCustom(struct os_mbuf *om);

static int _notify(uint16_t connHandle, uint16_t attrHandle, uint8_t* data, uint16_t len, ble_ota_char_id_t otaChar);

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);


/* > Local Variables *******************************************************************************/
static uint16_t s_connHandle;

static uint16_t s_receiveFwVal;
static uint16_t s_otaStatusVal;
static uint16_t s_commandVal;
static uint16_t s_customVal;

static ble_ota_char_t s_otaChars[BLE_OTA_CHAR_COUNT];


static bool s_otaInProgress = false;
static uint32_t s_curSector = 0;
static uint32_t s_curPacket = 0;
static uint32_t s_fwLen = 0;

static uint8_t s_fwBuf[OTA_BUFFER_SIZE];
static uint32_t s_fwBufOffset = 0;

static void (*s_receiveFwCallback)(uint8_t* data, uint16_t len) = NULL;
static void (*s_eventCallback)(uint8_t event) = NULL;
static uint16_t (*s_customCallback)(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen) = NULL;

static const struct ble_gatt_svc_def kOtaSvc[] = {
  {
  // Service
  .type = BLE_GATT_SVC_TYPE_PRIMARY,
  .uuid = BLE_UUID16_DECLARE(BLE_OTA_SERVICE_UUID),
  .characteristics = (struct ble_gatt_chr_def[]) { 
    // Characteristic
    {
      // Receive Firmare Characteristic
      .uuid = BLE_UUID16_DECLARE(RECV_FW_UUID),
      .access_cb = _otaSvcAccess,
      .val_handle = &s_receiveFwVal,
      .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE,
    }, {
      // OTA Status Characteristic
      .uuid = BLE_UUID16_DECLARE(OTA_BAR_UUID),
      .access_cb = _otaSvcAccess,
      .val_handle = &s_otaStatusVal,
      .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_READ,
    }, {
      // Command Characteristic
      .uuid = BLE_UUID16_DECLARE(COMMAND_UUID),
      .access_cb = _otaSvcAccess,
      .val_handle = &s_commandVal,
      .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE,
    }, {
      // Customer Characteristic
      .uuid = BLE_UUID16_DECLARE(CUSTOMER_UUID),
      .access_cb = _otaSvcAccess,
      .val_handle = &s_customVal,
      .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE,
    }, {
      0, /* No more characteristics in this service */
    } }
  },
  { 
    0, /* No more services */
  }
};

/* > Exported Variables ***************************************************************************/
ble_app_service_t g_otaService = {
  .gap = {
    .connectCb = NULL,
    .disconnectCb = NULL,
    .subscribeCb = BleOta_subscribe,
  },
  .init = BleOta_init,
  .deinit = BleOta_deinit,
  .gattService = kOtaSvc,
};


/* > Function Definitions *************************************************************************/

int BleOta_init(void) {
  // int rc;

  // rc = ble_gatts_count_cfg(kOtaSvc);
  // if (rc != 0) {
  //   return rc;
  // }

  // rc = ble_gatts_add_svcs(kOtaSvc);
  // if (rc != 0) {
  //   return rc;
  // }

  return 0;
}


void BleOta_deinit(void) {
  // TODO:
}


void BleOta_subscribe(struct ble_gap_event *event) {
  if(event->subscribe.attr_handle == s_receiveFwVal) {
    s_otaChars[BLE_OTA_CHAR_ID__RECV_FW].notifyEnabled = event->subscribe.cur_indicate;
    MODLOG_DFLT(INFO, "Receive FW notify: %d\n", s_otaChars[BLE_OTA_CHAR_ID__RECV_FW].notifyEnabled);
  }
  else if(event->subscribe.attr_handle == s_commandVal) {
    s_otaChars[BLE_OTA_CHAR_ID__COMMAND].notifyEnabled = event->subscribe.cur_indicate;
    MODLOG_DFLT(INFO, "Command notify: %d\n", s_otaChars[BLE_OTA_CHAR_ID__COMMAND].notifyEnabled);
  }
  else if(event->subscribe.attr_handle == s_otaStatusVal) {
    s_otaChars[BLE_OTA_CHAR_ID__OTA_BAR].notifyEnabled = event->subscribe.cur_indicate;
    MODLOG_DFLT(INFO, "OTA Status notify: %d\n", s_otaChars[BLE_OTA_CHAR_ID__OTA_BAR].notifyEnabled);
  }
  else if(event->subscribe.attr_handle == s_customVal) {
    s_otaChars[BLE_OTA_CHAR_ID__CUSTOM].notifyEnabled = event->subscribe.cur_indicate;
    MODLOG_DFLT(INFO, "Custom notify: %d\n", s_otaChars[BLE_OTA_CHAR_ID__CUSTOM].notifyEnabled);
  }
}



void BleOta_setEventCallback(void (*callback)(uint8_t event)) {
  s_eventCallback = callback;
}

void BleOta_setReceiveFwCallback(void (*callback)(uint8_t* data, uint16_t len)) {
  s_receiveFwCallback = callback;
}

void BleOta_setCustomCallback(uint16_t (*callback)(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen)) {
  s_customCallback = callback;
}

uint32_t BleOta_getFwLength(void) {
  return s_fwLen;
}


uint16_t BleOta_calculateCrc(const uint8_t *data, uint16_t len) {
  return crc16_ccitt(data, len);
}


/* > Local Functuon Definitions *******************************************************************/

static int _otaSvcAccess(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt,
                void *arg) 
{
  const ble_uuid_t *uuid;
  int rc;

  // TODO: Should put this in GAP?
  s_connHandle = conn_handle;

  switch (ctxt->op) {
    // --- Read Characteristic
    case BLE_GATT_ACCESS_OP_READ_CHR: {
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
          MODLOG_DFLT(INFO, "Characteristic read; conn_handle=%d attr_handle=%d\n",
                      conn_handle, attr_handle);
      } else {
          MODLOG_DFLT(INFO, "Characteristic read by NimBLE stack; attr_handle=%d\n",
                      attr_handle);
      }
      uuid = ctxt->chr->uuid;
      if (attr_handle == s_otaStatusVal) {
        // TODO: should send progress
        uint16_t x = 0xABCD;
        rc = os_mbuf_append(ctxt->om, &x, sizeof(x));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      }
    } break;

    // --- Write Characteristic
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        // MODLOG_DFLT(INFO, "Characteristic write; conn_handle=%d attr_handle=%d",
        //             conn_handle, attr_handle);
      } else {
        MODLOG_DFLT(INFO, "Characteristic write by NimBLE stack; attr_handle=%d",
                    attr_handle);
      }
      uuid = ctxt->chr->uuid;

      if (attr_handle == s_receiveFwVal)
        return _writeFw(ctxt->om);
      else if(attr_handle == s_commandVal)
        return _writeCommand(ctxt->om);
      else if(attr_handle == s_customVal)
        return _writeCustom(ctxt->om);
    } break;

    default: {
      MODLOG_DFLT(INFO, "Unknown characteristic %d, commandHandle %d\n", attr_handle, s_commandVal);
    } break;
  }

  /* Unknown characteristic/descriptor;
   * The NimBLE host should not have called this function;
   */
  assert(0);
  return BLE_ATT_ERR_UNLIKELY;
}



static int _writeCommand(struct os_mbuf *om) {

  // Check if received data length is correct
  if(om->om_len != sizeof(ble_ota_cmd_t)) {
    MODLOG_DFLT(ERROR, "Invalid command length: %d\n", om->om_len);
    return 0;
  }
  ble_ota_cmd_t* cmd = (ble_ota_cmd_t *)om->om_data;

  // Check CRC
  uint16_t crc = crc16_ccitt(om->om_data, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));
  if(crc != cmd->crc) {
    MODLOG_DFLT(ERROR, "CRC error: %02X != %02X\n", crc, cmd->crc);
    return 0;
  }

  // Process command
  bool accept = false;

  switch(cmd->cmdId) {
    case CMD_ID__START: {
      MODLOG_DFLT(INFO, "Start command received\n");
      s_otaInProgress = true;
      s_curSector = 0;
      s_curPacket = 0;
      s_fwLen = *((uint32_t*)&cmd->payload[0]);
      s_fwBufOffset = 0;
      accept = true;
      if(s_eventCallback != NULL) {
        s_eventCallback(1);
      }
    } break;

    case CMD_ID__STOP: {
      MODLOG_DFLT(INFO, "Stop command received\n");
      s_otaInProgress = false;
      accept = true;
    } break;

    default: {
      MODLOG_DFLT(ERROR, "Unknown command: %d\n", cmd->cmdId);
    } break;
  }

  // ACK
  ble_ota_cmd_t ack = { 0 };
  ack.cmdId = CMD_ID__ACK;
  *(uint16_t*)&ack.payload[0] = cmd->cmdId;
  *(uint16_t*)&ack.payload[2] = accept ? CMD_ACK__ACCEPT : CMD_ACK__REJECT;
  ack.crc = crc16_ccitt((uint8_t*)&ack, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));
  _notify(s_connHandle, s_commandVal, (uint8_t*)&ack, sizeof(ble_ota_cmd_t), BLE_OTA_CHAR_ID__COMMAND);

  return 0;
}


static int _writeFw(struct os_mbuf *om) {
  ble_ota_fw_header_t *header = (ble_ota_fw_header_t *)om->om_data;

  // Do not accept data if OTA not started
  if(!s_otaInProgress) {
    MODLOG_DFLT(ERROR, "OTA not in progress\n");
    return 0;
  }

  // Check if received data length is atleast the size of the header
  if(om->om_len < (sizeof(ble_ota_fw_header_t))) {
    MODLOG_DFLT(ERROR, "Invalid firmware packet length: %d\n", om->om_len);
    return 0;
  }

  ble_ota_ack_t ack = { 0 };

  // Check sector index
  if(header->sector != s_curSector) {
    // Sector error
    if(header->sector == 0xFFFF) {
      MODLOG_DFLT(INFO, "Last sector");
    }
    else {
      MODLOG_DFLT(ERROR, "Sector error, cur: %ld, recv: %d\n", s_curSector, header->sector);
      ack.sector = s_curSector;
      ack.ackStatus = ACK_STATUS__SECTOR_IDX_ERR;
      ack.currentSector = s_curSector;
      ack.crc = crc16_ccitt((uint8_t*)&ack, sizeof(ble_ota_ack_t) - sizeof(uint16_t));
      _notify(s_connHandle, s_receiveFwVal, (uint8_t*)&ack, sizeof(ble_ota_cmd_t), BLE_OTA_CHAR_ID__RECV_FW);
    }
  }

  // Check packet sequence
  if(header->packetSeq != s_curPacket) { // packet seq error
    if(header->packetSeq == 0xff) { // last packet
      MODLOG_DFLT(INFO, "Last packet");
    } else { // packet seq error
      MODLOG_DFLT(ERROR, "Packet index error, cur: %ld, recv: %d\n", s_curPacket, header->packetSeq);
    }
  }

  // Copy firmware data
  if(s_fwBufOffset + (om->om_len - 3) > OTA_BUFFER_SIZE) {
    MODLOG_DFLT(ERROR, "Firmware buffer overflow, cur: %ld, recv: %d\n", s_fwBufOffset, om->om_len - 3);

    // TODO: Allow to copy only the remaining space

    assert(0); // TODO
    return 0;
  }
  
  memcpy(s_fwBuf + s_fwBufOffset, om->om_data + 3, om->om_len - 3);
  s_fwBufOffset += om->om_len - 3;
  // MODLOG_DFLT(INFO, "Firmware received: %ld", s_fwBufOffset);

  if(SLIST_NEXT(om, om_next) != NULL) {
    struct os_mbuf *last;
    last = om;
    while (SLIST_NEXT(last, om_next) != NULL) {
      struct os_mbuf *temp = SLIST_NEXT(last, om_next);
      memcpy(s_fwBuf + s_fwBufOffset, temp->om_data, temp->om_len);
      s_fwBufOffset += temp->om_len;
      last = SLIST_NEXT(last, om_next);
      temp = NULL;
    }
  }

  MODLOG_DFLT(DEBUG, "DEBUG: Sector:%" PRIu32 ", total length:%" PRIu32 ", length:%d", 
    s_curSector, s_fwBufOffset, om->om_len - 3);

  if (header->packetSeq == 0xff) {
    s_curPacket = 0;
    s_curSector++;
    MODLOG_DFLT(DEBUG, "DEBUG: recv %" PRIu32 " sector", s_curSector);
  } else {
    MODLOG_DFLT(DEBUG, "DEBUG: wait next packet");
    s_curPacket++;
    return 0;
  }

  // Sector end
  MODLOG_DFLT(INFO, "Sector end, sector: %d, len: %ld", header->sector, s_fwBufOffset);
  if(s_receiveFwCallback != NULL) {
    if (s_fwBufOffset < OTA_BUFFER_SIZE) {
      s_receiveFwCallback(s_fwBuf, s_fwBufOffset);
    } else {
      s_receiveFwCallback(s_fwBuf, OTA_BLOCK_SIZE);
    }
  }

  // Reset Data Buffer
  s_fwBufOffset = 0;
  memset(s_fwBuf, 0x0, OTA_BUFFER_SIZE);

  // Send ACK
  ack.sector = header->sector;
  ack.ackStatus = ACK_STATUS__SUCCESS;
  ack.crc = crc16_ccitt((uint8_t*)&ack, sizeof(ble_ota_ack_t) - sizeof(uint16_t));
  _notify(s_connHandle, s_receiveFwVal, (uint8_t*)&ack, sizeof(ble_ota_cmd_t), BLE_OTA_CHAR_ID__RECV_FW);

  return 0;
}


static int _writeCustom(struct os_mbuf *om) {
  // We are reusing the cmd structure for custom commands
  MODLOG_DFLT(INFO, "Custom received: %d\n", om->om_len);

  if(s_customCallback != NULL) {
    // Call custom callback if set
    uint8_t resp[32] = { 0 };
    uint16_t respLen = s_customCallback(om->om_data, om->om_len, resp, sizeof(resp));
    
    // Notify with response
    if(respLen > 0) {
      _notify(s_connHandle, s_customVal, resp, respLen, BLE_OTA_CHAR_ID__CUSTOM);
    }
  }

  return 0;
}


static int _notify(uint16_t connHandle, uint16_t attrHandle, uint8_t* data, uint16_t len, ble_ota_char_id_t otaChar)
{
  struct os_mbuf *txom;
  bool notifyEnable = false;
  int rc;
  
  txom = ble_hs_mbuf_from_flat(data, len);

  if(otaChar < BLE_OTA_CHAR_COUNT) {
    if(s_otaChars[otaChar].notifyEnabled) {
      notifyEnable = true;
    }
  }

  if (notifyEnable) {
    rc = ble_gattc_notify_custom(connHandle, attrHandle, txom);
    if (rc == 0) {
      MODLOG_DFLT(DEBUG, "Notification sent, attr_handle = %d", attrHandle);
    } else {
      MODLOG_DFLT(ERROR, "Error in sending notification, rc = %d", rc);
    }
    return rc;
  }

  /* If notifications are disabled return ESP_FAIL */
  MODLOG_DFLT(INFO, "Notify is disabled");
  return ESP_FAIL;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
  uint16_t crc16 = 0;
  int32_t i;

  while (len--) {
    crc16 ^= *data++ << 8;

    for (i = 0; i < 8; i++) {
      if (crc16 & 0x8000) {
        crc16 = (crc16 << 1) ^ 0x1021;
      } else {
        crc16 = crc16 << 1;
      }
    }
  }

  return crc16;
}
