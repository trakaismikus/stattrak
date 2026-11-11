#ifndef _BLE_OTA_H
#define _BLE_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_hs.h"
#include "app_ble.h"

/* > Definitions **********************************************************************************/
#define OTA_BLOCK_SIZE                      (4096)
#define OTA_BUFFER_SIZE                     (OTA_BLOCK_SIZE + sizeof(uint16_t))

/* > Enumerations **********************************************************************************/
typedef enum {
  BLE_OTA_CHAR_ID__RECV_FW = 0,
  BLE_OTA_CHAR_ID__OTA_BAR,
  BLE_OTA_CHAR_ID__COMMAND,
  BLE_OTA_CHAR_ID__CUSTOM,

  BLE_OTA_CHAR__INVALID
} ble_ota_char_id_t;

#define BLE_OTA_CHAR_COUNT  (BLE_OTA_CHAR__INVALID)


/* > Type Definitions *******************************************************************************/

typedef struct {
  uint16_t  cmdId;
  uint8_t   payload[16];
  uint16_t  crc;
} ble_ota_cmd_t;// __attribute__((packed));


typedef struct {
  uint16_t sector;
  uint8_t packetSeq;
  // uint8_t payload[MTU - 7]
} ble_ota_fw_header_t;// __attribute__((packed));

typedef struct {
  uint16_t  sector;
  uint16_t  ackStatus;
  uint16_t  currentSector;
  uint8_t   dummy[12];
  uint16_t  crc;
} ble_ota_ack_t;

typedef struct {
  uint16_t valHandle;
  bool notifyEnabled;
} ble_ota_char_t;


/* > External Variables ****************************************************************************/
extern ble_app_service_t g_otaService;


/* > Function Declarations *************************************************************************/

int BleOta_init(void);
void BleOta_deinit(void);

void BleOta_subscribe(struct ble_gap_event *event);

void BleOta_setEventCallback(void (*callback)(uint8_t event));
void BleOta_setReceiveFwCallback(void (*callback)(uint8_t* data, uint16_t len));
void BleOta_setCustomCallback(uint16_t (*callback)(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen));
uint32_t BleOta_getFwLength(void);

uint16_t BleOta_calculateCrc(const uint8_t *data, uint16_t len);

#endif // _BLE_OTA_H