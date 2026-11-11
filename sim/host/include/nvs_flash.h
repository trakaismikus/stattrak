/*
 * Host stand-in for ESP-IDF nvs_flash.h / nvs.h.
 *
 * The storage is a small key-value table in memory (see host_nvs.c). It keeps
 * the NVS rules that matter for this firmware: keys are at most 15 characters,
 * a missing key returns ESP_ERR_NVS_NOT_FOUND, and a closed handle is invalid.
 */
#ifndef _HOST_NVS_FLASH_H
#define _HOST_NVS_FLASH_H

#include <stdint.h>
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
#define ESP_ERR_NVS_NO_FREE_PAGES       (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND   (ESP_ERR_NVS_BASE + 0x10)

#define NVS_KEY_NAME_MAX_SIZE           16  /* Includes the terminating zero */

typedef enum {
  NVS_READONLY,
  NVS_READWRITE
} nvs_open_mode_t;

typedef uint32_t nvs_handle_t;

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value);

#ifdef __cplusplus
}
#endif

#endif // _HOST_NVS_FLASH_H
