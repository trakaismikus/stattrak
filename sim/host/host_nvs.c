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

#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"

/* > Defines **************************************************************************************/
#define MAX_HANDLES   (4)

/* > Local Variables ******************************************************************************/
static host_nvs_entry_t s_entries[HOST_NVS_MAX_ENTRIES];
static uint32_t s_entryCount = 0;

/* Blobs: the data is on the heap of the computer (this is the stand-in, not the firmware) */
static host_nvs_blob_t s_blobs[HOST_NVS_MAX_BLOBS];
static uint32_t s_blobCount = 0;

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

static host_nvs_blob_t* _findBlob(const char* ns, const char* key) {
  for(uint32_t i = 0; i < s_blobCount; i++) {
    if(strcmp(s_blobs[i].ns, ns) == 0 && strcmp(s_blobs[i].key, key) == 0)
      return &s_blobs[i];
  }
  return NULL;
}

static uint32_t _blobBytes(void) {
  uint32_t total = 0;
  for(uint32_t i = 0; i < s_blobCount; i++)
    total += s_blobs[i].length;
  return total;
}

static void _removeBlob(host_nvs_blob_t* blob) {
  free(blob->data);
  const uint32_t idx = (uint32_t)(blob - s_blobs);
  s_blobCount--;
  memmove(&s_blobs[idx], &s_blobs[idx + 1], (s_blobCount - idx) * sizeof(s_blobs[0]));
}

static void _removeEntry(host_nvs_entry_t* entry) {
  const uint32_t idx = (uint32_t)(entry - s_entries);
  s_entryCount--;
  memmove(&s_entries[idx], &s_entries[idx + 1], (s_entryCount - idx) * sizeof(s_entries[0]));
}

/* The NVS entries of 32 bytes that the table uses (see nvs_get_stats in nvs_flash.h) */
#define PAGE_ENTRIES    126u
#define PAGE_COUNT      12u        /* 48 KB */
#define BLOB_PART_MAX   4000u
static uint32_t _blobEntries(uint32_t length) {
  const uint32_t parts = (length + BLOB_PART_MAX - 1) / BLOB_PART_MAX;
  return 1u + (parts ? parts : 1u) + (length + 31u) / 32u;
}
static uint32_t _namespaceCount(void) {
  char seen[HOST_NVS_MAX_ENTRIES + HOST_NVS_MAX_BLOBS][16];
  uint32_t count = 0;
  for(uint32_t i = 0; i < s_entryCount + s_blobCount; i++) {
    const char* ns = (i < s_entryCount) ? s_entries[i].ns : s_blobs[i - s_entryCount].ns;
    uint32_t j = 0;
    while(j < count && strcmp(seen[j], ns) != 0)
      j++;
    if(j == count)
      strcpy(seen[count++], ns);
  }
  return count;
}
static uint32_t _usedEntries(void) {
  uint32_t used = s_entryCount + _namespaceCount();
  for(uint32_t i = 0; i < s_blobCount; i++)
    used += _blobEntries(s_blobs[i].length);
  return used;
}
/* Room for `extra` more entries (one page stays free, as in the real NVS) */
static bool _fits(uint32_t extra) {
  return _usedEntries() + extra + PAGE_ENTRIES <= PAGE_ENTRIES * PAGE_COUNT;
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
    if(s_entryCount >= HOST_NVS_MAX_ENTRIES || !_fits(2))
      return false;
    entry = &s_entries[s_entryCount++];
    strcpy(entry->ns, ns);
    strcpy(entry->key, key);
  }

  entry->value = value;
  return true;
}

uint32_t HostNvs_getBlobCount(void)    { return s_blobCount; }

const host_nvs_blob_t* HostNvs_getBlob(uint32_t idx) {
  return (idx < s_blobCount) ? &s_blobs[idx] : NULL;
}

bool HostNvs_putBlob(const char* ns, const char* key, const uint8_t* data, uint32_t length) {
  if(strlen(ns) >= sizeof(s_blobs[0].ns) || strlen(key) >= sizeof(s_blobs[0].key) || length > HOST_NVS_MAX_BLOB)
    return false;

  host_nvs_blob_t* blob = _findBlob(ns, key);
  const uint32_t others = _blobBytes() - (blob ? blob->length : 0);
  if(others + length > HOST_NVS_BLOB_SPACE)
    return false;
  /* The real NVS writes the new blob before it erases the old one: the new one must fit */
  if(!_fits(_blobEntries(length) + 1u))
    return false;

  uint8_t* copy = (uint8_t*)malloc(length > 0 ? length : 1);
  if(copy == NULL)
    return false;
  if(length > 0)
    memcpy(copy, data, length);

  if(blob == NULL) {
    if(s_blobCount >= HOST_NVS_MAX_BLOBS) {
      free(copy);
      return false;
    }
    blob = &s_blobs[s_blobCount++];
    strcpy(blob->ns, ns);
    strcpy(blob->key, key);
  } else {
    free(blob->data);
  }

  blob->data = copy;
  blob->length = (uint16_t)length;
  return true;
}

void HostNvs_clear(void) {
  s_entryCount = 0;
  while(s_blobCount > 0)
    _removeBlob(&s_blobs[s_blobCount - 1]);
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

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(key == NULL || key[0] == '\0')
    return ESP_ERR_NVS_INVALID_NAME;
  if(strlen(key) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_KEY_TOO_LONG;
  if(value == NULL && length > 0)
    return ESP_ERR_INVALID_ARG;
  if(s_failWrites)
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

  if(!HostNvs_putBlob(s_handles[handle - 1].ns, key, (const uint8_t*)value, (uint32_t)length))
    return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

  s_writeCount++;
  HostState_markChanged();
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(key == NULL || length == NULL)
    return ESP_ERR_INVALID_ARG;
  if(strlen(key) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_KEY_TOO_LONG;

  const host_nvs_blob_t* blob = _findBlob(s_handles[handle - 1].ns, key);
  if(blob == NULL)
    return ESP_ERR_NVS_NOT_FOUND;

  /* The same as ESP-IDF: NULL asks for the size only, a short buffer is an error */
  if(out_value == NULL) {
    *length = blob->length;
    return ESP_OK;
  }
  if(*length < blob->length)
    return ESP_ERR_NVS_INVALID_LENGTH;

  memcpy(out_value, blob->data, blob->length);
  *length = blob->length;
  return ESP_OK;
}

esp_err_t nvs_get_stats(const char* part_name, nvs_stats_t* nvs_stats) {
  (void)part_name;
  if(nvs_stats == NULL)
    return ESP_ERR_INVALID_ARG;
  const uint32_t total = PAGE_ENTRIES * PAGE_COUNT;
  const uint32_t used = _usedEntries();
  nvs_stats->used_entries = used;
  nvs_stats->total_entries = total;
  nvs_stats->free_entries = (used < total) ? total - used : 0;
  nvs_stats->available_entries = (used + PAGE_ENTRIES < total) ? total - used - PAGE_ENTRIES : 0;
  nvs_stats->namespace_count = _namespaceCount();
  return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
  if(!_handleValid(handle))
    return ESP_ERR_NVS_INVALID_HANDLE;
  if(key == NULL)
    return ESP_ERR_INVALID_ARG;
  if(strlen(key) >= NVS_KEY_NAME_MAX_SIZE)
    return ESP_ERR_NVS_KEY_TOO_LONG;
  if(s_failWrites)
    return ESP_FAIL;

  host_nvs_blob_t* blob = _findBlob(s_handles[handle - 1].ns, key);
  host_nvs_entry_t* entry = _find(s_handles[handle - 1].ns, key);
  if(blob == NULL && entry == NULL)
    return ESP_ERR_NVS_NOT_FOUND;

  if(blob != NULL)
    _removeBlob(blob);
  if(entry != NULL)
    _removeEntry(entry);

  s_writeCount++;
  HostState_markChanged();
  return ESP_OK;
}
