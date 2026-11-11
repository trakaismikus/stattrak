
/* > Includes *************************************************************************************/
#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"
#include "app_ble.h"
#include "ble_ota.h"
#include "esp_app_desc.h"
#include "esp_mac.h"

/* > Enumerations *********************************************************************************/
enum custom_id_e {
  CUSTOM_ID__GET_VERSION  = 0x0001,
  CUSTOM_ID__GET_UID      = 0x0002,
  CUSTOM_ID__ACCEPT_FW    = 0x0003,
  CUSTOM_ID__REJECT_FW    = 0x0004,
  CUSTOM_ID__REBOOT       = 0x0005,
};


/* > Type Definitions *****************************************************************************/
typedef struct {
  uint8_t state;
  uint8_t progress;
} ota_state_t;

/* > Local Function Declarations ******************************************************************/
static void _eventCallback(uint8_t event);
static void _receiveFwCallback(uint8_t* data, uint16_t len);
static uint16_t _customCallback(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen);

static void _otaTask(void *pvParameters);

/* > Constants **************************************************************************************/
static const char *TAG = "OTA";


/* > Local Variables *******************************************************************************/
static esp_ota_handle_t s_otaHandle;
static esp_partition_t s_partition;

static TaskHandle_t s_taskHandle = NULL;
static RingbufHandle_t s_ringbuf = NULL;
static QueueHandle_t s_queue = NULL;

static ota_state_t s_otaState = { OTA_STATE__IDLE, 0 };

/* > Function Definitions *************************************************************************/

int Ota_init(void) {
  // Initialize ring buffer for OTA
  s_ringbuf = xRingbufferCreate(OTA_BLOCK_SIZE * 2, RINGBUF_TYPE_BYTEBUF);
  if(s_ringbuf == NULL) {
    ESP_LOGE(TAG, "Failed to create ring buffer");
    return ESP_FAIL;
  }

  // Initialize Queue for OTA progress
  s_queue = xQueueCreate(8, sizeof(ota_state_t));
  if(s_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create queue");
    return ESP_FAIL;
  }

  // Initialize OTA state
  s_otaState.state = OTA_STATE__IDLE;
  s_otaState.progress = 0;

  const esp_app_desc_t* app_desc = esp_app_get_description();
  ESP_LOGI(TAG, "App version: %s\n", app_desc->version);

  // Initialize BLE
  AppBle_init(&g_otaService);
  BleOta_setEventCallback(_eventCallback);
  BleOta_setReceiveFwCallback(_receiveFwCallback);
  BleOta_setCustomCallback(_customCallback);

  // Create Task for OTA
  xTaskCreate(_otaTask, "otaTask", 4096, NULL, 5, &s_taskHandle);
  if(s_taskHandle == NULL) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    return ESP_FAIL;
  }

  return ESP_OK;
}


void Ota_deinit(void) {
  // Deinit BLE
  AppBle_deinit();

  // Deinit OTA task if running
  if(s_taskHandle != NULL) {
    vTaskDelete(s_taskHandle);
  }

  if(s_ringbuf != NULL) {
    vRingbufferDelete(s_ringbuf);
  }

  if(s_queue != NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
  }
}


uint8_t Ota_getState(void) {
  if(s_queue == NULL) 
    return OTA_STATE__IDLE;

  // Check queue for state update
  while(xQueueReceive(s_queue, &s_otaState, 0) == pdTRUE);

  return s_otaState.state;
}

// Execute Ota_getState before calling this function
uint8_t Ota_getProgress(void) {
  // Check queue for state update
  // while(xQueueReceive(s_queue, &s_otaState, 0) == pdTRUE);

  return s_otaState.progress;
}


bool Ota_isPendingVerify(void) {
  
  esp_partition_t* partition = (esp_partition_t *)esp_ota_get_running_partition();
  if(partition == NULL) {
    ESP_LOGE(TAG, "Failed to get running partition");
    return false;
  }
  
  esp_ota_img_states_t state;
  esp_err_t res = esp_ota_get_state_partition(partition, &state);
  if(res != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get state of partition: %d", res);
    return false;
  }

  if(state == ESP_OTA_IMG_PENDING_VERIFY) {
    return true;
  }

  return false;
}


bool Ota_acceptPending(bool accept) {
  esp_partition_t* partition = (esp_partition_t *)esp_ota_get_running_partition();
  if(partition == NULL) {
    ESP_LOGE(TAG, "Failed to get running partition");
    return false;
  }

  esp_ota_img_states_t state;
  esp_err_t res = esp_ota_get_state_partition(partition, &state);
  if(res != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get state of partition: %d", res);
    return false;
  }

  if(state == ESP_OTA_IMG_PENDING_VERIFY) {
    if(accept) {
      if(esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid");
        return false;
      }
    } else {
      if(esp_ota_mark_app_invalid_rollback_and_reboot() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app invalid");
        return false;
      }
      ESP_LOGI(TAG, "App marked as invalid");
    }
    return true;
  }

  return false;
}



int Ota_start(void) {
  esp_partition_t *partitionPtr = NULL;
  const esp_partition_t *nextPartition = NULL;

  if(s_otaHandle != 0) {
    // Abort previos OTA
    if(esp_ota_abort(s_otaHandle) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to abort OTA");
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Previous OTA aborted");
  }


  partitionPtr = (esp_partition_t *)esp_ota_get_boot_partition();
  if(partitionPtr == NULL) {
    ESP_LOGE(TAG, "Failed to get boot partition");
    return ESP_FAIL;
  }

  if(partitionPtr->type != ESP_PARTITION_TYPE_APP) {
    ESP_LOGE(TAG, "Current partition is not APP");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Current partition: %s", partitionPtr->label);

  // Decide on the next partition to write to
  if(partitionPtr->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    s_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
  } else {
    nextPartition = esp_ota_get_next_update_partition(partitionPtr);
    if(nextPartition) {
      s_partition.subtype = nextPartition->subtype;
    } else {
      s_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    }
  }
  s_partition.type = ESP_PARTITION_TYPE_APP;

  partitionPtr = (esp_partition_t *)esp_partition_find_first(s_partition.type, s_partition.subtype, NULL);
  if(partitionPtr == NULL) {
    ESP_LOGE(TAG, "Failed to find partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Next partition to write to: %s", partitionPtr->label);

  memcpy(&s_partition, partitionPtr, sizeof(esp_partition_t));
  if(esp_ota_begin(&s_partition, OTA_SIZE_UNKNOWN, &s_otaHandle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to begin OTA\r\n");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Waiting for data...");


  return ESP_OK;
}


int Ota_write(const uint8_t* data, uint16_t len) {
  if(esp_ota_write(s_otaHandle, data, len) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write OTA data");
    return ESP_FAIL;
  }

  return ESP_OK;
}

void Ota_end(void) {
  if(esp_ota_end(s_otaHandle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to end OTA");
  }

  if(esp_ota_set_boot_partition(&s_partition) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed!\r\n");
  }
}


/* > Local Function Definitions *******************************************************************/

static void _otaTask(void *pvParameters) {
  size_t len;
  size_t totalLen = 0;
  uint8_t* data;
  bool otaStarted = false;
  bool otaEnded = false;

  while(!otaEnded) {
    data = (uint8_t*)xRingbufferReceive(s_ringbuf, &len, (TickType_t)portMAX_DELAY);

    if(data != NULL) {

      if(ulTaskNotifyTake(pdTRUE, 0)) {
        ESP_LOGI(TAG, "OTA started");

        xQueueSend(s_queue, &((ota_state_t){ OTA_STATE__STARTED, 0 }), 0);
  
        // Start OTA
        Ota_start();
        totalLen = 0;
        otaStarted = true;
        
        // vRingbufferReturnItem(s_ringbuf, (void*)data);
        // continue;
      }

      if(otaStarted) {
        if(Ota_write(data, len) != 0) {
          ESP_LOGE(TAG, "Failed to write OTA data");
        }

        totalLen += len;
        vRingbufferReturnItem(s_ringbuf, (void*)data);

        uint32_t progress = (totalLen * 100) / BleOta_getFwLength();
        xQueueSend(s_queue, &((ota_state_t){ OTA_STATE__WRITING, progress }), 0);

        if(totalLen >= BleOta_getFwLength()) {
          ESP_LOGI(TAG, "Firmware received: %d", totalLen);
          Ota_end();

          otaEnded = true;
        } else {
          ESP_LOGI(TAG, "Firmware received: %d, %d of %ld", len, totalLen, BleOta_getFwLength());
        }
      } else {
        vRingbufferReturnItem(s_ringbuf, (void*)data);
      }
    }
  }

  xQueueSend(s_queue, &((ota_state_t){ OTA_STATE__FINISHED, 100 }), portMAX_DELAY);

  s_taskHandle = NULL;
  vTaskDelete(NULL);
}


static void _eventCallback(uint8_t event) {
  ESP_LOGI(TAG, "Event: %d", event);

  if(event == 1) {
    TaskHandle_t mainTaskHandle = xTaskGetHandle("otaTask");
    if (mainTaskHandle != NULL) {
      // vRingbuffer(s_ringbuf);
      ESP_LOGI(TAG, "Notify task");

      // const char resetSignal = 0xFF;
      // xRingbufferSend(s_ringbuf, &resetSignal, sizeof(resetSignal), portMAX_DELAY);
      xRingbufferSend(s_ringbuf, NULL, 0, portMAX_DELAY);
      xTaskNotify(mainTaskHandle, 0, eIncrement);
    }
  }
}


static void _receiveFwCallback(uint8_t* data, uint16_t len) {
  // Write to ring buffer
  xRingbufferSend(s_ringbuf, (void*)data, len, (TickType_t)portMAX_DELAY);
}


static uint16_t _customCallback(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen) {
  // Check if received data length is correct
  if(len != sizeof(ble_ota_cmd_t)) {
    MODLOG_DFLT(ERROR, "Invalid command length: %d\n", len);
    return 0;
  }
  ble_ota_cmd_t* cmd = (ble_ota_cmd_t *)data;

  // Check CRC
  uint16_t crc = BleOta_calculateCrc(data, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));
  if(crc != cmd->crc) {
    MODLOG_DFLT(ERROR, "CRC error: %02X != %02X\n", crc, cmd->crc);
    return 0;
  }

  // --- Prepare response

  // Check if response buffer is valid
  if(resp == NULL || respLen < sizeof(ble_ota_cmd_t)) {
    MODLOG_DFLT(ERROR, "Invalid response buffer");
    return 0;
  }

  ble_ota_cmd_t* respCmd = (ble_ota_cmd_t *)resp;
  respCmd->cmdId = 0x0003; // ACK
  *((uint16_t*)&respCmd->payload[0]) = cmd->cmdId; // Echo command ID

  switch(cmd->cmdId) {
    case CUSTOM_ID__GET_VERSION: {
      // Respond with version
      const esp_app_desc_t* appDesc = esp_app_get_description();
      // TODO: should be numbers
      snprintf((char*)&respCmd->payload[2], sizeof(respCmd->payload), "%.13s", appDesc->version);
    } break;

    case CUSTOM_ID__GET_UID: {
      // Respond with unique ID
      uint8_t uid[6] = { 0 };
      esp_efuse_mac_get_default(uid);
      memcpy(&respCmd->payload[2], uid, sizeof(uid));
      ESP_LOGI(TAG, "Unique ID: %02X%02X%02X%02X%02X%02X",
               uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);
    } break;

    case CUSTOM_ID__ACCEPT_FW: {
      // Accept firmware
      respCmd->payload[2] = Ota_acceptPending(true);
      xQueueSend(s_queue, &((ota_state_t){ OTA_STATE__ACCEPTED, 100 }), portMAX_DELAY);
    } break;

    case CUSTOM_ID__REJECT_FW: {
      // Reject firmware
      respCmd->payload[2] = Ota_acceptPending(false);
    } break;

    case CUSTOM_ID__REBOOT: {
      // Reboot device
      respCmd->payload[2] = 1; // Accept
      // TODO: Implement actual reboot logic
    } break;

    default: {
      ESP_LOGE(TAG, "Unknown custom command ID: %d", cmd->cmdId);
    } return 0;
  }

  // Calculate CRC for response
  respCmd->crc = BleOta_calculateCrc((uint8_t*)respCmd, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));

  return sizeof(ble_ota_cmd_t);
}