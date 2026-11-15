/***************************************************************************************************
 * @file    : data_store.c
 * @brief   : The flash namespace "data". See data_store.h and docs/DATA.md.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "data_store.h"

#include "esp_log.h"
#include "nvs_flash.h"

/* > Local Variables ******************************************************************************/
static const char* TAG = "DataStore";

static nvs_handle_t s_handle;
static bool         s_open = false;

/* > Function Definitions *************************************************************************/

void DataStore_init(void) {
  DataStore_deinit();
  if(nvs_open("data", NVS_READWRITE, &s_handle) == ESP_OK)
    s_open = true;
  else
    ESP_LOGE(TAG, "Cannot open the data storage");
}

void DataStore_deinit(void) {
  if(s_open) {
    nvs_close(s_handle);
    s_open = false;
  }
}

bool DataStore_isOpen(void) {
  return s_open;
}

bool DataStore_read(const char* key, void* data, size_t* length) {
  size_t size = 0;
  if(!s_open || key == NULL || data == NULL || length == NULL)
    return false;
  if(nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK || size != *length)
    return false;
  size_t read = size;
  return nvs_get_blob(s_handle, key, data, &read) == ESP_OK && read == size;
}

bool DataStore_readAny(const char* key, void* data, size_t max, size_t* length) {
  size_t size = 0;
  if(!s_open || key == NULL || data == NULL || length == NULL)
    return false;
  if(nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK || size > max)
    return false;
  size_t read = size;
  if(nvs_get_blob(s_handle, key, data, &read) != ESP_OK || read != size)
    return false;
  *length = size;
  return true;
}

bool DataStore_write(const char* key, const void* data, size_t length) {
  if(!s_open || key == NULL || nvs_set_blob(s_handle, key, data, length) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot write %s", key ? key : "-");
    return false;
  }
  return nvs_commit(s_handle) == ESP_OK;
}

void DataStore_erase(const char* key) {
  if(s_open && key != NULL) {
    nvs_erase_key(s_handle, key);   // ESP_ERR_NVS_NOT_FOUND is good too
    nvs_commit(s_handle);
  }
}

uint32_t DataStore_availableEntries(void) {
  nvs_stats_t stats;
  if(nvs_get_stats(NULL, &stats) != ESP_OK)
    return 0;
  return (uint32_t)stats.available_entries;
}
