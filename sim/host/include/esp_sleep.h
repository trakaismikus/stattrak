/*
 * Host stand-in for ESP-IDF esp_sleep.h. The simulated device never sleeps:
 * a power-off ends the simulated session instead.
 */
#ifndef _HOST_ESP_SLEEP_H
#define _HOST_ESP_SLEEP_H

#include "esp_err.h"

typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_GPIO = 7,
} esp_sleep_wakeup_cause_t;

static inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) {
  return ESP_SLEEP_WAKEUP_UNDEFINED;
}

#endif // _HOST_ESP_SLEEP_H
