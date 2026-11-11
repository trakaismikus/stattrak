/*
 * Host stand-in for the espressif/button component (button_gpio.h, v4 API).
 */
#ifndef _HOST_BUTTON_GPIO_H
#define _HOST_BUTTON_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t gpio_num;
  uint8_t active_level;
  bool    enable_power_save;
  bool    disable_pull;
} button_gpio_config_t;

esp_err_t iot_button_new_gpio_device(const button_config_t* button_config, const button_gpio_config_t* gpio_cfg,
                                     button_handle_t* ret_button);

#ifdef __cplusplus
}
#endif

#endif // _HOST_BUTTON_GPIO_H
