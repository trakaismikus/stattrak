
/* > Includes *************************************************************************************/
#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"
#include "app_ble.h"
#include "ble_ota.h"
#include "esp_app_desc.h"
#include "esp_mac.h"

/* > Defines **************************************************************************************/
#define OTA_POLL_MS                   (100)
#define OTA_DATA_TIMEOUT_MS           (15 * 1000)  // Abort if no data received for this long
#define OTA_RINGBUF_SEND_TIMEOUT_MS   (10 * 1000)
#define OTA_STATE_SEND_TIMEOUT_MS     (100)
#define OTA_TASK_EXIT_TIMEOUT_MS      (2000)

#define OTA_USER_QUEUE_LENGTH         (4)          // User commands that wait for the main loop

// OTA task notification bits
#define NOTIFY__START   (1 << 0)
#define NOTIFY__STOP    (1 << 1)
#define NOTIFY__FAIL    (1 << 2)
#define NOTIFY__EXIT    (1 << 3)

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
static bool _eventCallback(ble_ota_event_t event);
static void _receiveFwCallback(uint8_t* data, uint16_t len);
static uint16_t _customCallback(uint8_t* data, uint16_t len, uint8_t* resp, uint16_t respLen);

static void _otaTask(void *pvParameters);
static void _postState(uint8_t state, uint8_t progress, TickType_t wait);
static void _fail(void);

/* > Constants **************************************************************************************/
static const char *TAG = "OTA";


/* > Local Variables *******************************************************************************/
static esp_ota_handle_t s_otaHandle = 0;
static const esp_partition_t* s_partition = NULL;

static TaskHandle_t volatile s_taskHandle = NULL;
static RingbufHandle_t s_ringbuf = NULL;
static QueueHandle_t s_queue = NULL;
static QueueHandle_t s_userQueue = NULL;

static volatile bool s_updateAllowed = true;

static ota_state_t s_otaState = { OTA_STATE__IDLE, 0 };

/* > Function Definitions *************************************************************************/

int Ota_init(void) {
  // Already initialized
  if(s_queue != NULL) {
    ESP_LOGW(TAG, "Already initialized");
    s_updateAllowed = true;
    return ESP_OK;
  }

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
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
    return ESP_FAIL;
  }

  // Queue for user commands. The update works without it, so a failure here is not an error.
  s_userQueue = xQueueCreate(OTA_USER_QUEUE_LENGTH, sizeof(ota_user_cmd_t));
  if(s_userQueue == NULL) {
    ESP_LOGW(TAG, "No queue for user commands");
  }

  // Initialize OTA state
  s_otaState.state = OTA_STATE__IDLE;
  s_otaState.progress = 0;
  s_updateAllowed = true;

  const esp_app_desc_t* app_desc = esp_app_get_description();
  ESP_LOGI(TAG, "App version: %s\n", app_desc->version);

  // Create Task for OTA (before BLE, so callbacks always have a task to notify)
  TaskHandle_t taskHandle = NULL;
  xTaskCreate(_otaTask, "otaTask", 4096, NULL, 5, &taskHandle);
  if(taskHandle == NULL) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    if(s_userQueue != NULL) {
      vQueueDelete(s_userQueue);
      s_userQueue = NULL;
    }
    vQueueDelete(s_queue);
    s_queue = NULL;
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
    return ESP_FAIL;
  }
  s_taskHandle = taskHandle;

  // Initialize BLE
  BleOta_setEventCallback(_eventCallback);
  BleOta_setReceiveFwCallback(_receiveFwCallback);
  BleOta_setCustomCallback(_customCallback);
  AppBle_init(&g_otaService);

  return ESP_OK;
}


void Ota_deinit(void) {
  if(s_queue == NULL)
    return;

  // Deinit BLE, no more callbacks after this
  AppBle_deinit();

  // Ask OTA task to exit, so it's never deleted in the middle of a flash operation
  if(s_taskHandle != NULL) {
    xTaskNotify(s_taskHandle, NOTIFY__EXIT, eSetBits);

    for(uint32_t t = 0; s_taskHandle != NULL && t < OTA_TASK_EXIT_TIMEOUT_MS; t += 10) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if(s_taskHandle != NULL) {
      ESP_LOGE(TAG, "OTA task did not exit, deleting");
      vTaskDelete(s_taskHandle);
      s_taskHandle = NULL;
    }
  }

  Ota_abort();
  BleOta_abort();

  if(s_ringbuf != NULL) {
    vRingbufferDelete(s_ringbuf);
    s_ringbuf = NULL;
  }

  if(s_userQueue != NULL) {
    vQueueDelete(s_userQueue);
    s_userQueue = NULL;
  }

  if(s_queue != NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
  }

  s_otaState.state = OTA_STATE__IDLE;
  s_otaState.progress = 0;
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
  // Abort previous OTA
  Ota_abort();

  // Next OTA partition after the running one (factory -> ota_0, ota_0 <-> ota_1)
  s_partition = esp_ota_get_next_update_partition(NULL);
  if(s_partition == NULL) {
    ESP_LOGE(TAG, "Failed to find partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Next partition to write to: %s", s_partition->label);

  // Erase sectors incrementally while writing instead of the whole partition up front
  esp_err_t err = esp_ota_begin(s_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_otaHandle);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(err));
    s_otaHandle = 0;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Waiting for data...");

  return ESP_OK;
}


int Ota_write(const uint8_t* data, uint16_t len) {
  esp_err_t err = esp_ota_write(s_otaHandle, data, len);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
    return ESP_FAIL;
  }

  return ESP_OK;
}


int Ota_end(void) {
  // esp_ota_end() frees the handle also on failure
  esp_err_t err = esp_ota_end(s_otaHandle);
  s_otaHandle = 0;
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(s_partition);
  if(err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    return ESP_FAIL;
  }

  return ESP_OK;
}


void Ota_abort(void) {
  if(s_otaHandle != 0) {
    if(esp_ota_abort(s_otaHandle) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to abort OTA");
    } else {
      ESP_LOGI(TAG, "OTA aborted");
    }
    s_otaHandle = 0;
  }
}


void Ota_setUpdateAllowed(bool allowed) {
  s_updateAllowed = allowed;
}


bool Ota_getUserCommand(ota_user_cmd_t* cmd) {
  if(s_userQueue == NULL || cmd == NULL)
    return false;

  return (xQueueReceive(s_userQueue, cmd, 0) == pdTRUE);
}


bool Ota_sendUserResponse(uint16_t cmdId, uint8_t status, const uint8_t* data, uint8_t len) {
  if(s_queue == NULL || len > OTA_USER_RESPONSE_SIZE || (data == NULL && len > 0))
    return false;

  // The same packet as the answer to an update command: ACK, the command number, then the data
  ble_ota_cmd_t resp = { 0 };
  resp.cmdId = 0x0003; // ACK
  resp.payload[0] = (uint8_t)(cmdId & 0xFF);
  resp.payload[1] = (uint8_t)(cmdId >> 8);
  resp.payload[2] = status;
  if(len > 0)
    memcpy(&resp.payload[3], data, len);
  resp.crc = BleOta_calculateCrc((uint8_t*)&resp, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));

  return (BleOta_sendCustom((const uint8_t*)&resp, sizeof(resp)) == 0);
}


/* > Local Function Definitions *******************************************************************/

static void _otaTask(void *pvParameters) {
  bool active = false;
  uint32_t fwLen = 0;
  uint32_t totalLen = 0;
  TickType_t lastDataTick = 0;

  while(1) {
    size_t len = 0;
    uint8_t* data = (uint8_t*)xRingbufferReceive(s_ringbuf, &len, pdMS_TO_TICKS(OTA_POLL_MS));

    // Check notifications after receive, so a START is always seen before its first data
    uint32_t notify = 0;
    xTaskNotifyWait(0, UINT32_MAX, &notify, 0);

    if(notify & NOTIFY__EXIT) {
      if(data != NULL)
        vRingbufferReturnItem(s_ringbuf, (void*)data);
      break;
    }

    if((notify & (NOTIFY__STOP | NOTIFY__FAIL)) && active) {
      active = false;
      if(notify & NOTIFY__FAIL) {
        _fail();
      } else {
        ESP_LOGW(TAG, "OTA stopped");
        Ota_abort();
        _postState(OTA_STATE__IDLE, 0, pdMS_TO_TICKS(OTA_STATE_SEND_TIMEOUT_MS));
      }
    }

    if(notify & NOTIFY__START) {
      ESP_LOGI(TAG, "OTA started");

      fwLen = BleOta_getFwLength();
      totalLen = 0;
      lastDataTick = xTaskGetTickCount();
      active = (Ota_start() == ESP_OK);

      if(active)
        _postState(OTA_STATE__STARTED, 0, 0);
      else
        _fail();
    }

    if(data != NULL) {
      if(active) {
        lastDataTick = xTaskGetTickCount();

        if(Ota_write(data, len) != ESP_OK) {
          active = false;
          _fail();
        } else {
          totalLen += len;

          if(totalLen >= fwLen) {
            ESP_LOGI(TAG, "Firmware received: %" PRIu32, totalLen);
            active = false;

            if(Ota_end() == ESP_OK)
              _postState(OTA_STATE__FINISHED, 100, pdMS_TO_TICKS(OTA_STATE_SEND_TIMEOUT_MS));
            else
              _fail();
          } else {
            ESP_LOGI(TAG, "Firmware received: %d, %" PRIu32 " of %" PRIu32, len, totalLen, fwLen);
            _postState(OTA_STATE__WRITING, (totalLen * 100) / fwLen, 0);
          }
        }
      }

      vRingbufferReturnItem(s_ringbuf, (void*)data);
    }
    else if(active && (xTaskGetTickCount() - lastDataTick) >= pdMS_TO_TICKS(OTA_DATA_TIMEOUT_MS)) {
      ESP_LOGE(TAG, "Timeout waiting for firmware data");
      active = false;
      _fail();
    }
  }

  Ota_abort();

  s_taskHandle = NULL;
  vTaskDelete(NULL);
}


static void _postState(uint8_t state, uint8_t progress, TickType_t wait) {
  xQueueSend(s_queue, &((ota_state_t){ state, progress }), wait);
}


static void _fail(void) {
  Ota_abort();
  BleOta_abort();
  _postState(OTA_STATE__ERROR, 0, pdMS_TO_TICKS(OTA_STATE_SEND_TIMEOUT_MS));
}


static bool _eventCallback(ble_ota_event_t event) {
  ESP_LOGI(TAG, "Event: %d", event);

  if(s_taskHandle == NULL)
    return false;

  switch(event) {
    case BLE_OTA_EVENT__START: {
      // A view that uses Bluetooth for an other feature can refuse new firmware
      if(!s_updateAllowed) {
        ESP_LOGW(TAG, "Firmware update is not allowed now");
        return false;
      }

      // Reject images that don't fit in the next OTA partition
      const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
      if(partition == NULL || BleOta_getFwLength() > partition->size) {
        ESP_LOGE(TAG, "Firmware does not fit: %" PRIu32, BleOta_getFwLength());
        return false;
      }

      xTaskNotify(s_taskHandle, NOTIFY__START, eSetBits);
    } return true;

    case BLE_OTA_EVENT__STOP:
      xTaskNotify(s_taskHandle, NOTIFY__STOP, eSetBits);
      return true;

    default:
      return false;
  }
}


static void _receiveFwCallback(uint8_t* data, uint16_t len) {
  // Write to ring buffer
  if(xRingbufferSend(s_ringbuf, (void*)data, len, pdMS_TO_TICKS(OTA_RINGBUF_SEND_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to queue firmware data");
    BleOta_abort();
    if(s_taskHandle != NULL)
      xTaskNotify(s_taskHandle, NOTIFY__FAIL, eSetBits);
  }
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
      snprintf((char*)&respCmd->payload[2], sizeof(respCmd->payload) - 2, "%.13s", appDesc->version);
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
      bool accepted = Ota_acceptPending(true);
      respCmd->payload[2] = accepted;
      if(accepted)
        _postState(OTA_STATE__ACCEPTED, 100, pdMS_TO_TICKS(OTA_STATE_SEND_TIMEOUT_MS));
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
      // Command numbers from OTA_USER_CMD_FIRST are for user features. This function runs in
      // the Bluetooth task, so the command only goes into a queue here. The main loop takes
      // it with Ota_getUserCommand() and answers with Ota_sendUserResponse().
      if(cmd->cmdId >= OTA_USER_CMD_FIRST && s_userQueue != NULL) {
        ota_user_cmd_t userCmd;
        userCmd.cmdId = cmd->cmdId;
        memcpy(userCmd.data, cmd->payload, sizeof(userCmd.data));
        if(xQueueSend(s_userQueue, &userCmd, 0) != pdTRUE) {
          ESP_LOGW(TAG, "User command queue is full");
        }
      } else {
        ESP_LOGE(TAG, "Unknown custom command ID: %d", cmd->cmdId);
      }
    } return 0;
  }

  // Calculate CRC for response
  respCmd->crc = BleOta_calculateCrc((uint8_t*)respCmd, sizeof(ble_ota_cmd_t) - sizeof(uint16_t));

  return sizeof(ble_ota_cmd_t);
}
