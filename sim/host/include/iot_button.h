/*
 * Host stand-in for the espressif/button component (iot_button.h, v4 API).
 *
 * host_button.c runs the same press / long-press / repeat state machine as the
 * real component, driven by simulated time. The device file
 * main/board/bsp_button.c is compiled against this header without changes.
 */
#ifndef _HOST_IOT_BUTTON_H
#define _HOST_IOT_BUTTON_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BUTTON_PRESS_DOWN = 0,
  BUTTON_PRESS_UP,
  BUTTON_PRESS_REPEAT,
  BUTTON_PRESS_REPEAT_DONE,
  BUTTON_SINGLE_CLICK,
  BUTTON_DOUBLE_CLICK,
  BUTTON_MULTIPLE_CLICK,
  BUTTON_LONG_PRESS_START,
  BUTTON_LONG_PRESS_HOLD,
  BUTTON_LONG_PRESS_UP,
  BUTTON_PRESS_END,
  BUTTON_EVENT_MAX,
  BUTTON_NONE_PRESS,
} button_event_t;

typedef struct button_dev_t* button_handle_t;

typedef void (*button_cb_t)(void* button_handle, void* usr_data);

typedef struct {
  uint16_t long_press_time;   /* ms, 0 = use CONFIG_BUTTON_LONG_PRESS_TIME_MS */
  uint16_t short_press_time;  /* ms, 0 = use CONFIG_BUTTON_SHORT_PRESS_TIME_MS */
} button_config_t;

typedef void button_event_args_t;

esp_err_t      iot_button_register_cb(button_handle_t btn, button_event_t event, button_event_args_t* event_args,
                                      button_cb_t cb, void* usr_data);
esp_err_t      iot_button_delete(button_handle_t btn);
button_event_t iot_button_get_event(button_handle_t btn);
uint8_t        iot_button_get_key_level(button_handle_t btn);
esp_err_t      iot_button_resume(void);
esp_err_t      iot_button_stop(void);

#ifdef __cplusplus
}
#endif

#endif // _HOST_IOT_BUTTON_H
