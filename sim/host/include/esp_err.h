/*
 * Host stand-in for ESP-IDF esp_err.h. Only what the StatTrak firmware uses.
 */
#ifndef _HOST_ESP_ERR_H
#define _HOST_ESP_ERR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                  -1

#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_INVALID_SIZE      0x104
#define ESP_ERR_NOT_FOUND         0x105
#define ESP_ERR_NOT_SUPPORTED     0x106
#define ESP_ERR_TIMEOUT           0x107

const char* esp_err_to_name(esp_err_t code);

/* On the device a failed ESP_ERROR_CHECK aborts and reboots. Here it ends the
 * simulated session with reason "abort", so a test can see it. */
void Host_errorCheckFailed(esp_err_t code, const char* expr, const char* file, int line);

#define ESP_ERROR_CHECK(x) do {                                         \
    esp_err_t _hostErr = (x);                                           \
    if(_hostErr != ESP_OK) {                                            \
      Host_errorCheckFailed(_hostErr, #x, __FILE__, __LINE__);          \
    }                                                                   \
  } while(0)

#ifdef __cplusplus
}
#endif

#endif // _HOST_ESP_ERR_H
