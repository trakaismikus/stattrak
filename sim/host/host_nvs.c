/***************************************************************************************************
 * @file    : host_nvs.c
 * @brief   : Stand-in for the ESP-IDF NVS flash storage.
 *
 * A small key-value table. A front end or a test can make writes fail, can make the open fail,
 * and can put any value in the table before the firmware starts (for example a value that an
 * other firmware version stored).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"

#include <string.h>

#include "nvs_flash.h"

/* > Defines **************************************************************************************/
#define MAX_HANDLES   (4)

/* > Local Variables ******************************************************************************/
static host_nvs_entry_t s_entries[HOST_NVS_MAX_ENTRIES];
static uint32_t s_entryCount = 0;

static struct {
  bool open;
  char ns[16];
} s_handles[MAX_HANDLES];

static bool s_initialized = false;
static bool s_failWrites = false;
static bool s_failOpen = false;
static bool s_noFreePagesOnce = false;

static uint32_t s_writeCount = 0;
static uint32_t s_commitCount = 0;
static uint32_t s_eraseCount = 0;

/* > Local Functions ******************************************************************************/

static host_nvs_entry_t* _find(const char* ns, const char* key) {
  for(uint32_t i = 0; i < s_entryCount; i++) {
    if(strcmp(s_entries[i].ns, ns) == 0 && strcmp(s_entries[i].key, key) == 0)
      return &s_entries[i];
  }
  return NULL;
}

static bool _handleValid(nvs_handle_t handle) {
  return (handle >= 1 && handle <= MAX_HANDLES && s_handles[handle - 1].open);
}

/* > Front end API ********************************************************************************/

void HostNvs_setFailWrites(bool fail)         { s_failWrites = fail; }
void HostNvs_setFailOpen(bool fail)           { s_failOpen = fail; }
void HostNvs_setNoFreePagesOnce(bool enable)  { s_noFreePagesOnce = enable; }

uint32_t HostNvs_getWriteCount(void)   { return s_writeCount; }
uint32_t HostNvs_getCommitCount(void)  { return s_commitCount; }
uint32_t HostNvs_getEraseCount(void)   { return s_eraseCount; }
uint32_t HostNvs_getEntryCount(void)   { return s_entryCount; }

const host_nvs_entry_t* HostNvs_getEntry(uint32_t idx) {
  return (idx < s_entryCount) ? &s_entries[idx] : NULL;
}

bool HostNvs_put(const char* ns, const char* key, uint32_t value) {
  if(strlen(ns) >= sizeof(s_entries[0].ns) || strlen(key) >= sizeof(s_entries[0].key))
    return false;

  host_nvs_entry_t* entry = _find(ns, key);
  if(entry == NULL) {
    if(s_entryCount >= HOST_NVS_MAX_ENTRIES)
      return false;
    entry = &s_entries[s_entryCount++];
    strcpy(entry->ns, ns);
    strcpy(entry->key, key);
  }

  entry->value = value;
  return true;
}

void HostNvs_clear(void) {
  s_entryCount = 0;
}

/* > ESP-IDF API **********************************************************************************/

esp_err_t nvs_flash_init(void) {
  if(s_noFreePagesOnce) {
    s_noFreePagesOnce = false;
    return ESP_ERR_NVS_NO_FREE_PAGES;
  }

  s_initialized = true;
  return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
  /* The real function fails while the partition is in use. The firmware closes its handle first. */
  HostNvs_clear();
  memset(s_handles, 0, sizeof(s_handles));
  s_initialized = false;
  s_eraseCount++;
  HostState_markChanged();
  return ESP_OK;
}

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle) {
  (void)open_mode;

  if(!s_initialized)
    return ESP_ERR_NVS_NOT_INITIALIZED;
  if(s_failOpen)
    return ESP_FAIL;
  if(namespace_name == NULL || out_handle == NULL)
    return ESP_ERR_INVALID_ARG;
  if(strlen(namespace_name) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_INVALID_NAME;

  for(uint32_t i = 0; i < MAX_HANDLES; i++) {
    if(!s_handles[i].open) {
      s_handles[i].open = true;
      strcpy(s_handles[i].ns, namespace_name);
      *out_handle = i + 1;
      return ESP_OK;
    }
  }

  return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t handle) {
  if(_handleValid(handle))
    s_handles[handle - 1].open = false;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(s_failWrites)
    return ESP_FAIL;

  s_commitCount++;
  return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(key == NULL || key[0] == '\0')
    return ESP_ERR_NVS_INVALID_NAME;
  if(strlen(key) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_KEY_TOO_LONG;
  if(s_failWrites)
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

  /* The real NVS writes to flash here. nvs_commit() does not have to follow for the value to stay. */
  if(!HostNvs_put(s_handles[handle - 1].ns, key, value))
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

  s_writeCount++;
  HostState_markChanged();
  return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(key == NULL || out_value == NULL)
    return ESP_ERR_INVALID_ARG;
  if(strlen(key) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_KEY_TOO_LONG;

  const host_nvs_entry_t* entry = _find(s_handles[handle - 1].ns, key);
  if(entry == NULL)
    return ESP_ERR_NVS_NOT_FOUND;

  *out_value = entry->value;
  return ESP_OK;
}
