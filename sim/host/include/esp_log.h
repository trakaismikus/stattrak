/*
 * Host stand-in for ESP-IDF esp_log.h.
 * Log lines go to stderr, and only when logging is switched on (--log).
 */
#ifndef _HOST_ESP_LOG_H
#define _HOST_ESP_LOG_H

#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void Host_log(char level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
  __attribute__((format(printf, 3, 4)))
#endif
  ;

#define ESP_LOGE(tag, fmt, ...)   Host_log('E', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...)   Host_log('W', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...)   Host_log('I', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)   Host_log('D', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...)   Host_log('V', tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // _HOST_ESP_LOG_H
