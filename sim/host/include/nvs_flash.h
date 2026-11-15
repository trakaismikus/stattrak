/*
 * Host stand-in for ESP-IDF nvs_flash.h / nvs.h.
 *
 * The storage is a small key-value table in memory (see host_nvs.c). It keeps
 * the NVS rules that matter for this firmware: keys are at most 15 characters,
 * a missing key returns ESP_ERR_NVS_NOT_FOUND, and a closed handle is invalid.
 * Blobs (byte arrays) are kept too, with a limit for all blobs together.
 */
#ifndef _HOST_NVS_FLASH_H
#define _HOST_NVS_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH       (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY           (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE    (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME        (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE      (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_KEY_TOO_LONG        (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES       (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND   (ESP_ERR_NVS_BASE + 0x10)

#define NVS_KEY_NAME_MAX_SIZE           16  /* Includes the terminating zero */

typedef enum {
  NVS_READONLY,
  NVS_READWRITE
} nvs_open_mode_t;

typedef uint32_t nvs_handle_t;

/* nvs_get_stats(): the entries of 32 bytes. The stand-in counts them as the real NVS does (about):
   a u32 = 1 entry, a blob = 1 index entry + for each part of at most 4000 bytes 1 entry + its data
   in entries of 32 bytes, a namespace = 1 entry. 12 pages of 126 entries; one page stays free. */
typedef struct {
  size_t used_entries;
  size_t free_entries;
  size_t available_entries;   /* free_entries without the page that the NVS keeps free */
  size_t total_entries;
  size_t namespace_count;
} nvs_stats_t;
esp_err_t nvs_get_stats(const char* part_name, nvs_stats_t* nvs_stats);

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value);

/* Blobs: a byte array for each key. With out_value NULL, nvs_get_blob() only writes the size. */
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key);

#ifdef __cplusplus
}
#endif

#endif // _HOST_NVS_FLASH_H
