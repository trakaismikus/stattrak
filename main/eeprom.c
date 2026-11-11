/***************************************************************************************************
 * @file    : eeprom.c
 * @brief   : EEPROM emulator source file
 * @author  : Janis Zaharans
 * @date    : Apr 8, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "eeprom.h"

#include <string.h>
#include "esp_log.h"

/* > Defines **************************************************************************************/

/* > Enumerations *********************************************************************************/

/* > Type Definitions *****************************************************************************/

/* > Local Function Declarations ******************************************************************/
static void _readEntries(void);

/* > Constants ************************************************************************************/
const char* TAG = "EEPROM";

/* > Local Variables ******************************************************************************/

static eeprom_t* s_eeprom;

/* > Function Definitions *************************************************************************/

void Eeprom_init(eeprom_t* eeprom) {
  s_eeprom = eeprom;
  
  // Initialize NVS
  esp_err_t ret = nvs_open("eeprom", NVS_READWRITE, &s_eeprom->nvsHandle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
    return;
  }
  ESP_LOGI(TAG, "NVS handle opened successfully");

  // Read EEPROM entries from Flash
  _readEntries();
}


void Eeprom_update(void) {
  if(s_eeprom->updateNeeded) {
    if(nvs_commit(s_eeprom->nvsHandle) == ESP_OK)
      ESP_LOGI(TAG, "EEPROM updated successfully");
    else
      ESP_LOGE(TAG, "Failed to update EEPROM");
    s_eeprom->updateNeeded = false;
  }
}


uint32_t Eeprom_read(uint32_t id) {
  // Check if id is in range
  if(id >= s_eeprom->entryCount)
    return 0;
  
  return s_eeprom->entries[id];
}


void Eeprom_write(uint32_t id, uint32_t value) {
  // Check if id is in range
  if(id >= s_eeprom->entryCount)
    return;

  if(s_eeprom->entries[id] != value) {
    s_eeprom->entries[id] = value;
    nvs_set_u32(s_eeprom->nvsHandle, s_eeprom->entryKeys[id], value);
    s_eeprom->updateNeeded = true;
  }
}

uint32_t Eeprom_increment(uint32_t id, uint32_t maxValue) {
  // Check if id is in range
  if(id >= s_eeprom->entryCount)
    return 0;

  if(s_eeprom->entries[id] < maxValue) {
    s_eeprom->entries[id]++;
    nvs_set_u32(s_eeprom->nvsHandle, s_eeprom->entryKeys[id], s_eeprom->entries[id]);
    s_eeprom->updateNeeded = true;
  }

  return s_eeprom->entries[id];
}

uint32_t Eeprom_decrement(uint32_t id, uint32_t minValue) {
  // Check if id is in range
  if(id >= s_eeprom->entryCount)
    return 0;

  if(s_eeprom->entries[id] > minValue) {
    s_eeprom->entries[id]--;
    nvs_set_u32(s_eeprom->nvsHandle, s_eeprom->entryKeys[id], s_eeprom->entries[id]);
    s_eeprom->updateNeeded = true;
  }

  return s_eeprom->entries[id];
}


void Eeprom_format(const uint32_t* defaults) {
  // Close handle
  nvs_close(s_eeprom->nvsHandle);

  // Erase NVS Flash
  if(nvs_flash_erase() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to erase NVS Flash");
    return;
  }

  if(nvs_flash_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reinitialize NVS Flash");
    return;
  }

  // Open NVS handle again
  Eeprom_init(s_eeprom);

  // Set defaults
  Eeprom_writeDefaults(defaults);
}


void Eeprom_writeDefaults(const uint32_t* defaults) {
  if(defaults != NULL) {
    for(uint32_t i=0; i<s_eeprom->entryCount; i++) {
      Eeprom_write(i, defaults[i]);
    }

    if(nvs_commit(s_eeprom->nvsHandle) == ESP_OK) {
      ESP_LOGI(TAG, "Defaults written successfully");
    } else {
      ESP_LOGE(TAG, "Failed to write defaults");
    }
  }
}


/* > Local Function Definitions *******************************************************************/

static void _readEntries(void) {
  for(uint32_t i = 0; i < s_eeprom->entryCount; i++) {
    uint32_t value;
    esp_err_t err = nvs_get_u32(s_eeprom->nvsHandle, s_eeprom->entryKeys[i], &value);
    
    if(err == ESP_OK) {
      s_eeprom->entries[i] = value;
      ESP_LOGI(TAG, "%s: %ld", s_eeprom->entryKeys[i], value);
    } else if(err == ESP_ERR_NVS_NOT_FOUND) {
      // Key not found, initialize with default value
      s_eeprom->entries[i] = 0;  // or some other default value
      ESP_LOGI(TAG, "%s not found", s_eeprom->entryKeys[i]);
    } else {
      ESP_LOGE(TAG, "Error reading key %s: %s", s_eeprom->entryKeys[i], esp_err_to_name(err));
    }
  }
  ESP_LOGI(TAG, "EEPROM entries read successfully");
}