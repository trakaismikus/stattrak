
/* > Includes *************************************************************************************/
#include "bsp_button.h"
#include "board.h"

#include "iot_button.h"
#include "button_gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "cyclic_buf.h"
#include "app.h"

/* > Enumerations *********************************************************************************/
enum {
  BTN_BOTH_STATE__IDLE = 0,
  BTN_BOTH_STATE__PRESSED,
  BTN_BOTH_STATE__LONG_PRESS,
};

/* > Type Definitions *****************************************************************************/
typedef struct {
  uint8_t state;
  uint8_t ignoreNextClick;
} button_state_t;

/* > Local Function Declarations ******************************************************************/
static void _buttonEventCallback(void *arg, void *data);
static void _processButtons(uint8_t btnId, button_event_t event);
static inline void _addEvent(uint8_t event);

/* > Constants ************************************************************************************/
static const char *TAG = "[BSP Button]";

static const button_event_t kButtonEvents[] = {
  BUTTON_PRESS_DOWN,
  BUTTON_PRESS_UP,
  BUTTON_PRESS_REPEAT,
  BUTTON_PRESS_REPEAT_DONE,
  // BUTTON_SINGLE_CLICK,
  // BUTTON_DOUBLE_CLICK,
  BUTTON_LONG_PRESS_START,
  BUTTON_LONG_PRESS_HOLD,
  // BUTTON_LONG_PRESS_UP,
  // BUTTON_PRESS_END
};

/* > Local Variables ******************************************************************************/
static button_handle_t s_btnHandles[2];
static button_state_t s_btnStates[2];
static uint8_t s_bothState = BTN_BOTH_STATE__IDLE;

static uint8_t s_btnEventBuf[32];
static cyclic_buf_t s_btnEvents;


/* > Function Definitions *************************************************************************/

void BspButton_init(void) {
  esp_err_t ret;

  CyclicBuf_init(&s_btnEvents, s_btnEventBuf, sizeof(s_btnEventBuf));

  button_config_t btn_cfg = {0};
  button_gpio_config_t gpio_cfg = {
    .gpio_num = BTN_PIN,
    .active_level = 0,
    .enable_power_save = true,
  };

  ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_btnHandles[0]);
  assert(ret == ESP_OK);

  gpio_cfg.gpio_num = BTN2_PIN;
  ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_btnHandles[1]);
  assert(ret == ESP_OK);


  // Register the button event callback
  ret = ESP_OK;
  for(int i = 0; i < sizeof(kButtonEvents) / sizeof(kButtonEvents[0]); i++) {
    ret = iot_button_register_cb(s_btnHandles[0], kButtonEvents[i], NULL, _buttonEventCallback, s_btnHandles[0]);
    ret = iot_button_register_cb(s_btnHandles[1], kButtonEvents[i], NULL, _buttonEventCallback, s_btnHandles[1]);
  }
  assert(ret == ESP_OK);

  iot_button_resume();
}


void BspButton_deinit(void) {
  iot_button_delete(s_btnHandles[0]);
  iot_button_delete(s_btnHandles[1]);
}

void BspButton_clearEvents(void) {
  while(!CyclicBuf_isEmpty(&s_btnEvents)) {
    CyclicBuf_read(&s_btnEvents);
  }
}

uint8_t BspButton_getEvent(void) {
  if(CyclicBuf_isEmpty(&s_btnEvents)) {
    return EVENT__NONE;
  }

  // Read the next event from the cyclic buffer
  return CyclicBuf_read(&s_btnEvents);
}

uint8_t BspButton_getState(uint8_t btnId) {
  return iot_button_get_key_level(s_btnHandles[btnId]);
}


/* > Local Function Definitions *******************************************************************/
static void _buttonEventCallback(void *arg, void *data) {
  button_handle_t btn = (button_handle_t)arg;
  // iot_button_print_event(btn);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    ESP_LOGI(TAG, "Wake up from light sleep, reason %d", cause);
  }

  uint8_t btnId = (btn == s_btnHandles[0]) ? 0 : 1;
  _processButtons(btnId, iot_button_get_event(btn));
}


static void _processButtons(uint8_t btnId, button_event_t event) {
  ESP_LOGI(TAG, "Button %d event: %d", btnId + 1, event);
  
  if(event == BUTTON_PRESS_DOWN) {
    s_btnStates[btnId].state = 1;
    _addEvent(EVENT__RAW_PRESS_1 + btnId);
  } else if(event == BUTTON_PRESS_UP){
    s_btnStates[btnId].state = 0;
    _addEvent(EVENT__RAW_RELEASE_1 + btnId);

    if(s_bothState == BTN_BOTH_STATE__PRESSED) {
      s_btnStates[btnId].ignoreNextClick = 1;
    }
  }

  // Check if both buttons are released
  if(s_bothState > BTN_BOTH_STATE__IDLE && s_btnStates[0].state == 0 && s_btnStates[1].state == 0) {
    if(s_bothState == BTN_BOTH_STATE__PRESSED) {
      _addEvent(EVENT__PRESS_BOTH);
      ESP_LOGI(TAG, "Both button click");
    }

    s_bothState = BTN_BOTH_STATE__IDLE;
    return;
  }

  // Check if both buttons are pressed
  if(s_btnStates[!btnId].state == 1 && event == BUTTON_PRESS_DOWN) {
    s_bothState = BTN_BOTH_STATE__PRESSED;
    return;
  }

  switch(event) {
    // Button click
    case BUTTON_PRESS_REPEAT:
    case BUTTON_PRESS_REPEAT_DONE:
      if(s_btnStates[btnId].ignoreNextClick) {
        s_btnStates[btnId].ignoreNextClick = 0;
        break; // Ignore this click
      }
      _addEvent(EVENT__PRESS_1 + btnId);
      ESP_LOGI(TAG, "Button %d single click", btnId + 1);
      break;

    // Button long press start
    case BUTTON_LONG_PRESS_START:
      if(s_bothState == BTN_BOTH_STATE__IDLE) {
        _addEvent(EVENT__LONG_PRESS_1 + btnId);
        ESP_LOGI(TAG, "Button %d long press start", btnId + 1);
      } else if(s_bothState == BTN_BOTH_STATE__PRESSED) {
        _addEvent(EVENT__LONG_PRESS_B);
        ESP_LOGI(TAG, "Both buttons long press");
        s_bothState = BTN_BOTH_STATE__LONG_PRESS;
      }
      break;

    // Button long press hold
    case BUTTON_LONG_PRESS_HOLD:
      if(s_bothState == BTN_BOTH_STATE__IDLE) {
        _addEvent(EVENT__LONG_HOLD_1 + btnId);
        ESP_LOGI(TAG, "Button %d long press hold", btnId + 1);
      }
      break;

    default: break;
  }
}


static inline void _addEvent(uint8_t event) {
  if(!CyclicBuf_isFull(&s_btnEvents)) {
    CyclicBuf_write(&s_btnEvents, event);
  } else {
    ESP_LOGW(TAG, "Button event buffer is full, dropping event %d", event);
  }
}
