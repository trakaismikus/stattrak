/***************************************************************************************************
 * @file    : host_button.c
 * @brief   : Stand-in for the espressif/button component.
 *
 * This is the same state machine as button_handler() in the real component (v4), without the
 * extra callback argument features that the StatTrak firmware does not use. It runs once each
 * CONFIG_BUTTON_PERIOD_TIME_MS of simulated time. The timing values come from
 * sdkconfig.defaults, so a change there also changes the simulator.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"

#include <string.h>

#include "sdkconfig.h"
#include "iot_button.h"
#include "button_gpio.h"

/* > Defines **************************************************************************************/
#define BUTTON_COUNT      (2)
#define MAX_CB_PER_EVENT  (2)

#define TICKS_INTERVAL    (CONFIG_BUTTON_PERIOD_TIME_MS)
#define DEBOUNCE_TICKS    (CONFIG_BUTTON_DEBOUNCE_TICKS)
#define SHORT_TICKS       (CONFIG_BUTTON_SHORT_PRESS_TIME_MS / TICKS_INTERVAL)
#define LONG_TICKS        (CONFIG_BUTTON_LONG_PRESS_TIME_MS / TICKS_INTERVAL)
#define SERIAL_TICKS      (CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS / TICKS_INTERVAL)

#if (HOST_STEP_MS != CONFIG_BUTTON_PERIOD_TIME_MS)
  #error "HOST_STEP_MS must be equal to CONFIG_BUTTON_PERIOD_TIME_MS"
#endif

/* > Enumerations *********************************************************************************/
enum {
  PRESS_DOWN_CHECK = 0,
  PRESS_UP_CHECK,
  PRESS_REPEAT_DOWN_CHECK,
  PRESS_REPEAT_UP_CHECK,
  PRESS_LONG_PRESS_UP_CHECK,
};

/* > Type Definitions *****************************************************************************/
typedef struct {
  button_cb_t cb;
  void*       usrData;
} button_cb_info_t;

struct button_dev_t {
  bool      used;
  bool      rawLevel;         /* Level that the front end set (true = pressed) */
  uint8_t   buttonLevel;      /* Level after debounce */
  uint8_t   debounceCnt;
  uint8_t   state;
  uint8_t   event;
  uint8_t   repeat;
  uint32_t  ticks;
  uint32_t  longPressHoldCnt;
  button_cb_info_t cbs[BUTTON_EVENT_MAX][MAX_CB_PER_EVENT];
};

/* > Local Variables ******************************************************************************/
static struct button_dev_t s_btns[BUTTON_COUNT];
static bool s_running = false;

/* > Local Function Declarations ******************************************************************/
static void _callCbs(struct button_dev_t* btn, button_event_t event);
static void _handler(struct button_dev_t* btn);

/* > Front end API ********************************************************************************/

void HostButton_setLevel(uint8_t id, bool pressed) {
  if(id < BUTTON_COUNT)
    s_btns[id].rawLevel = pressed;
}

bool HostButton_getLevel(uint8_t id) {
  return (id < BUTTON_COUNT) ? s_btns[id].rawLevel : false;
}

void HostButton_tick(void) {
  if(!s_running)
    return;

  for(uint8_t i = 0; i < BUTTON_COUNT; i++) {
    if(s_btns[i].used)
      _handler(&s_btns[i]);
  }
}

/* > espressif/button API *************************************************************************/

esp_err_t iot_button_new_gpio_device(const button_config_t* button_config, const button_gpio_config_t* gpio_cfg,
                                     button_handle_t* ret_button) {
  (void)button_config;
  (void)gpio_cfg;

  if(ret_button == NULL)
    return ESP_ERR_INVALID_ARG;

  for(uint8_t i = 0; i < BUTTON_COUNT; i++) {
    if(!s_btns[i].used) {
      bool rawLevel = s_btns[i].rawLevel; /* Keep the level: a button can be held before boot */
      memset(&s_btns[i], 0, sizeof(s_btns[i]));
      s_btns[i].rawLevel = rawLevel;
      s_btns[i].used = true;
      s_btns[i].event = BUTTON_NONE_PRESS;
      *ret_button = &s_btns[i];
      s_running = true; /* The real component starts its timer when a button is created */
      return ESP_OK;
    }
  }

  return ESP_ERR_NO_MEM;
}

esp_err_t iot_button_register_cb(button_handle_t btn, button_event_t event, button_event_args_t* event_args,
                                 button_cb_t cb, void* usr_data) {
  (void)event_args;

  if(btn == NULL || event >= BUTTON_EVENT_MAX || cb == NULL)
    return ESP_ERR_INVALID_ARG;

  for(uint8_t i = 0; i < MAX_CB_PER_EVENT; i++) {
    if(btn->cbs[event][i].cb == NULL) {
      btn->cbs[event][i].cb = cb;
      btn->cbs[event][i].usrData = usr_data;
      return ESP_OK;
    }
  }

  return ESP_ERR_NO_MEM;
}

esp_err_t iot_button_delete(button_handle_t btn) {
  if(btn == NULL)
    return ESP_ERR_INVALID_ARG;

  btn->used = false;
  return ESP_OK;
}

button_event_t iot_button_get_event(button_handle_t btn) {
  return (btn != NULL) ? (button_event_t)btn->event : BUTTON_NONE_PRESS;
}

uint8_t iot_button_get_key_level(button_handle_t btn) {
  return (btn != NULL && btn->rawLevel) ? 1 : 0;
}

esp_err_t iot_button_resume(void) {
  s_running = true;
  return ESP_OK;
}

esp_err_t iot_button_stop(void) {
  s_running = false;
  return ESP_OK;
}

/* > Local Function Definitions *******************************************************************/

static void _callCbs(struct button_dev_t* btn, button_event_t event) {
  for(uint8_t i = 0; i < MAX_CB_PER_EVENT; i++) {
    if(btn->cbs[event][i].cb != NULL)
      btn->cbs[event][i].cb(btn, btn->cbs[event][i].usrData);
  }
}

static void _handler(struct button_dev_t* btn) {
  const uint8_t readLevel = btn->rawLevel ? 1 : 0;

  if(btn->state > 0)
    btn->ticks++;

  /* Debounce */
  if(readLevel != btn->buttonLevel) {
    if(++(btn->debounceCnt) >= DEBOUNCE_TICKS) {
      btn->buttonLevel = readLevel;
      btn->debounceCnt = 0;
    }
  } else {
    btn->debounceCnt = 0;
  }

  switch(btn->state) {
    case PRESS_DOWN_CHECK:
      if(btn->buttonLevel) {
        btn->event = BUTTON_PRESS_DOWN;
        _callCbs(btn, BUTTON_PRESS_DOWN);
        btn->ticks = 0;
        btn->repeat = 1;
        btn->state = PRESS_UP_CHECK;
      } else {
        btn->event = BUTTON_NONE_PRESS;
      }
      break;

    case PRESS_UP_CHECK:
      if(!btn->buttonLevel) {
        btn->event = BUTTON_PRESS_UP;
        _callCbs(btn, BUTTON_PRESS_UP);
        btn->ticks = 0;
        btn->state = PRESS_REPEAT_DOWN_CHECK;
      } else if(btn->ticks >= LONG_TICKS) {
        btn->event = BUTTON_LONG_PRESS_START;
        btn->state = PRESS_LONG_PRESS_UP_CHECK;
        _callCbs(btn, BUTTON_LONG_PRESS_START);
      }
      break;

    case PRESS_REPEAT_DOWN_CHECK:
      if(btn->buttonLevel) {
        btn->event = BUTTON_PRESS_DOWN;
        _callCbs(btn, BUTTON_PRESS_DOWN);
        btn->event = BUTTON_PRESS_REPEAT;
        btn->repeat++;
        _callCbs(btn, BUTTON_PRESS_REPEAT);
        btn->ticks = 0;
        btn->state = PRESS_REPEAT_UP_CHECK;
      } else if(btn->ticks > SHORT_TICKS) {
        if(btn->repeat == 1) {
          btn->event = BUTTON_SINGLE_CLICK;
          _callCbs(btn, BUTTON_SINGLE_CLICK);
        } else if(btn->repeat == 2) {
          btn->event = BUTTON_DOUBLE_CLICK;
          _callCbs(btn, BUTTON_DOUBLE_CLICK);
        }

        btn->event = BUTTON_PRESS_REPEAT_DONE;
        _callCbs(btn, BUTTON_PRESS_REPEAT_DONE);
        btn->repeat = 0;
        btn->state = PRESS_DOWN_CHECK;
        btn->event = BUTTON_PRESS_END;
        _callCbs(btn, BUTTON_PRESS_END);
      }
      break;

    case PRESS_REPEAT_UP_CHECK:
      if(!btn->buttonLevel) {
        btn->event = BUTTON_PRESS_UP;
        _callCbs(btn, BUTTON_PRESS_UP);
        if(btn->ticks < SHORT_TICKS) {
          btn->ticks = 0;
          btn->state = PRESS_REPEAT_DOWN_CHECK;
        } else {
          btn->state = PRESS_DOWN_CHECK;
          btn->event = BUTTON_PRESS_END;
          _callCbs(btn, BUTTON_PRESS_END);
        }
      }
      break;

    case PRESS_LONG_PRESS_UP_CHECK:
      if(btn->buttonLevel) {
        if(btn->ticks >= ((btn->longPressHoldCnt + 1) * SERIAL_TICKS + LONG_TICKS)) {
          btn->event = BUTTON_LONG_PRESS_HOLD;
          btn->longPressHoldCnt++;
          _callCbs(btn, BUTTON_LONG_PRESS_HOLD);
        }
      } else {
        btn->event = BUTTON_LONG_PRESS_UP;
        _callCbs(btn, BUTTON_LONG_PRESS_UP);

        btn->event = BUTTON_PRESS_UP;
        _callCbs(btn, BUTTON_PRESS_UP);
        btn->state = PRESS_DOWN_CHECK;
        btn->longPressHoldCnt = 0;
        btn->event = BUTTON_PRESS_END;
        _callCbs(btn, BUTTON_PRESS_END);
      }
      break;

    default:
      btn->state = PRESS_DOWN_CHECK;
      break;
  }
}
