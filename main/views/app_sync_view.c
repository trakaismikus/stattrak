/***************************************************************************************************
 * @file    : app_sync_view.c
 * @brief   : Settings > "Apps: SYNC". See app_sync_view.h and docs/APPS.md, section 6.
 *
 * Bluetooth is on (Ota_init) and the device refuses new firmware while this view is open
 * (Ota_setUpdateAllowed(false)): nobody near the person can send a firmware here. The update
 * view calls Ota_init() again, and that allows firmware again.
 *
 * An install: BEGIN, then ASK shows "INSTALL <NAME>?" before any data comes. Hold button 2 = yes:
 * then the page sends the data, and END checks it and writes it to flash. Hold button 1 = no.
 * Only a hold that starts while the question shows counts, and the button fills while it is held.
 * After the install: press 1 = SYNC MORE (wait for the next app), press 2 = OPEN (the app opens,
 * and Bluetooth goes off with this view). Long press 1 at other times = back.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_sync_view.h"

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "app_host_view.h"
#include "apps.h"
#include "gfx.h"
#include "font.h"
#include "time.h"
#include "ota.h"
#include "app_ble_name.h"
#include "sdkconfig.h"
#include "esp_log.h"

/* > Defines **************************************************************************************/
#define SCREEN_W        128
#define BUTTON_H        9
#define BUTTON_Y        21
#define BUTTON_MARGIN   4
#define HOLD_MS         CONFIG_BUTTON_LONG_PRESS_TIME_MS   // The fill is full when the long press comes

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);
static void _reset(void);
static void _answer(bool yes);
static void _drawButton(int16_t x, int16_t w, const char* text, uint8_t fill);
static uint8_t _holdFill(uint8_t button);

/* > Local Variables ******************************************************************************/
static const char* TAG = "AppSync";

view_t g_appSyncView = {
  .init = _init,
  .deinit = _deinit,
  .draw = _draw,
  .eventHandler = _eventHandler,
};

static app_sync_state_t s_state = APP_SYNC_STATE__IDLE;
static bool         s_begun = false;      // BEGIN came: ASK can come
static bool         s_failed = false;     // The last install failed after "yes": the display says so
static uint32_t     s_id = 0;
static uint32_t     s_size = 0;
static uint32_t     s_crc = 0;
static uint32_t     s_received = 0;       // Bytes in the buffer = the next expected offset
static uint32_t     s_questionStart = 0;
static uint32_t     s_lastDataMs = 0;     // A transfer that stops (page closed, out of range) ends after APP_SYNC_DATA_MS
static stapp_info_t s_info;
static char         s_name[STAPP_MAX_NAME + 1];
static uint8_t      s_cancel = 0;
// A hold of button 1 / 2 that started while the question shows (others do not answer it)
static bool         s_holding[2] = { false, false };
static uint32_t     s_holdStart[2] = { 0, 0 };

/* > Function Definitions *************************************************************************/

bool AppSync_isActive(void) {
  return AppView_get() == VIEW_ID__APP_SYNC && AppView_getNext() == VIEW_ID__APP_SYNC;
}

app_sync_state_t AppSync_getState(void) {
  return s_state;
}

bool AppSync_isWaiting(void) {
  return s_state == APP_SYNC_STATE__WAITING;
}

app_sync_result_t AppSync_begin(uint32_t id, uint32_t size, uint32_t crc) {
  if(s_state == APP_SYNC_STATE__WAITING)
    return APP_SYNC_ERR_WAITING;

  switch(Apps_canInstall(id, size)) {
    case APPS_OK: break;
    case APPS_ERR_TOO_LARGE: return APP_SYNC_ERR_TOO_LARGE;
    case APPS_ERR_TOO_MANY:  return APP_SYNC_ERR_TOO_MANY;
    case APPS_ERR_NO_SPACE:  return APP_SYNC_ERR_NO_SPACE;
    default:                 return APP_SYNC_ERR_DATA;
  }
  if(size < STAPP_HANDLERS_OFFSET + 2 * STAPP_HANDLERS_V1 + 4)
    return APP_SYNC_ERR_DATA;   // Smaller than the smallest package

  // A new install: an earlier "yes" does not count for it
  _reset();
  s_id = id;
  s_size = size;
  s_crc = crc;
  s_begun = true;
  s_failed = false;
  return APP_SYNC_OK;
}

app_sync_result_t AppSync_ask(const uint8_t* name, uint8_t length) {
  if(s_state == APP_SYNC_STATE__WAITING)
    return APP_SYNC_ERR_WAITING;
  if(!s_begun || s_state == APP_SYNC_STATE__RECEIVING || !Stapp_isNameField(name, length))
    return APP_SYNC_ERR_DATA;

  memcpy(s_name, name, STAPP_MAX_NAME);
  s_name[STAPP_MAX_NAME] = '\0';
  s_begun = false;
  s_holding[0] = s_holding[1] = false;
  s_questionStart = Time_getMillis();
  s_state = APP_SYNC_STATE__WAITING;
  App_click(1000);
  return APP_SYNC_OK;
}

app_sync_result_t AppSync_data(uint16_t offset, const uint8_t* data, uint8_t length, uint16_t* expected) {
  if(expected)
    *expected = (uint16_t)s_received;
  if(s_state != APP_SYNC_STATE__RECEIVING || data == NULL)
    return APP_SYNC_ERR_DATA;
  if(offset != s_received)
    return APP_SYNC_ERR_OFFSET;

  // The last part of the package can be shorter than the command data
  uint32_t count = length;
  if(count > s_size - s_received)
    count = s_size - s_received;

  uint8_t* buffer = Apps_getWorkBuffer();
  memcpy(&buffer[s_received], data, count);
  s_received += count;
  s_lastDataMs = Time_getMillis();

  if(expected)
    *expected = (uint16_t)s_received;
  return APP_SYNC_OK;
}

app_sync_result_t AppSync_end(stapp_result_t* reason, uint16_t* expected) {
  const uint8_t* buffer = Apps_getWorkBuffer();

  if(reason)
    *reason = STAPP_OK;
  if(expected)
    *expected = (uint16_t)s_received;
  if(s_state != APP_SYNC_STATE__RECEIVING)
    return APP_SYNC_ERR_DATA;
  if(s_received != s_size)
    return APP_SYNC_ERR_OFFSET;

  // A failed check ends this install: the page starts again with BEGIN (and the question)
  if(Stapp_crc32(buffer, s_size) != s_crc) {
    _reset();
    s_failed = true;
    return APP_SYNC_ERR_CRC;
  }

  // The person said yes to this id and this name: the package must have both
  stapp_result_t check = Stapp_check(buffer, s_size, &s_info);
  if(check == STAPP_OK && (s_info.id != s_id || strcmp(s_info.name, s_name) != 0))
    check = STAPP_ERR_HEADER;
  if(check != STAPP_OK) {
    ESP_LOGW(TAG, "Package refused: %s", Stapp_resultText(check));
    if(reason)
      *reason = check;
    _reset();
    s_failed = true;
    return APP_SYNC_ERR_PACKAGE;
  }

  // The one flash write of the package
  const apps_result_t result = Apps_install(buffer, &s_info);
  if(result != APPS_OK) {
    ESP_LOGE(TAG, "Install failed: %d", (int)result);
    _reset();
    s_failed = true;
    return APP_SYNC_ERR_FLASH;
  }

  s_state = APP_SYNC_STATE__INSTALLED;
  App_click(1200);
  return APP_SYNC_OK;
}

void AppSync_update(uint32_t now) {
  // Signed: a Bluetooth command can set the start time a little after `now` was read
  if(s_state == APP_SYNC_STATE__WAITING && (int32_t)(now - s_questionStart) >= APP_SYNC_QUESTION_MS)
    s_state = APP_SYNC_STATE__TIMEOUT;

  // The page stopped sending: wait for a new install
  if(s_state == APP_SYNC_STATE__RECEIVING && (int32_t)(now - s_lastDataMs) >= APP_SYNC_DATA_MS) {
    _reset();
    s_failed = true;
  }
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  _reset();
  s_failed = false;
  s_cancel = 0;

  // The name that the person gave this device (Bluetooth name, docs/APPS.md section 6), for this
  // Bluetooth start only. The update view and the accept view are always "StatTrak".
  char name[APPS_DEVICE_NAME_MAX + 1];
  Apps_getDeviceName(name);
  AppBle_setNextName(name);

  Ota_init();
  // A view of a user feature: nobody can send a firmware while it is open (docs/BLUETOOTH.md)
  Ota_setUpdateAllowed(false);
}

static void _deinit(void) {
  Ota_deinit();
  _reset();
}

static void _reset(void) {
  s_state = APP_SYNC_STATE__IDLE;
  s_begun = false;
  s_received = 0;
  s_size = 0;
  s_holding[0] = s_holding[1] = false;
}

static void _answer(bool yes) {
  if(s_state != APP_SYNC_STATE__WAITING)
    return;

  s_holding[0] = s_holding[1] = false;
  if(!yes) {
    s_state = APP_SYNC_STATE__REFUSED;
    App_click(800);
    return;
  }

  // Yes: now the page sends the data
  s_received = 0;
  s_lastDataMs = Time_getMillis();
  s_state = APP_SYNC_STATE__RECEIVING;
  App_click(1200);
}

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1:
      if(s_state == APP_SYNC_STATE__INSTALLED)
        s_state = APP_SYNC_STATE__IDLE;   // SYNC MORE: wait for the next app
      s_failed = false;
      return true;

    case EVENT__PRESS_2:
      if(s_state == APP_SYNC_STATE__INSTALLED) {
        // OPEN: as the app menu opens an app. Bluetooth goes off with this view (_deinit).
        AppHost_setApp(s_id);
        AppView_set(VIEW_ID__APP_HOST);
      }
      s_failed = false;
      return true;

    case EVENT__LONG_PRESS_1:
      if(s_state == APP_SYNC_STATE__WAITING) {
        if(s_holding[0])
          _answer(false);
      } else if(s_state == APP_SYNC_STATE__RECEIVING) {
        _reset();   // Stop this transfer; the page must start again with BEGIN
        s_failed = true;
      } else {
        AppView_set(AppView_getPrevious());
      }
      return true;

    case EVENT__LONG_PRESS_2:
      if(s_state == APP_SYNC_STATE__WAITING && s_holding[1])
        _answer(true);
      return true;

    case EVENT__RAW_PRESS_1:
    case EVENT__RAW_PRESS_2: {
      const uint8_t button = (event == EVENT__RAW_PRESS_1) ? 0 : 1;
      if(button == 0)
        s_cancel = 1;
      if(s_state == APP_SYNC_STATE__WAITING) {
        s_holding[button] = true;
        s_holdStart[button] = Time_getMillis();
      }
    } break;

    case EVENT__RAW_RELEASE_1:
    case EVENT__RAW_RELEASE_2: {
      const uint8_t button = (event == EVENT__RAW_RELEASE_1) ? 0 : 1;
      if(button == 0)
        s_cancel = 0;
      s_holding[button] = false;
    } break;

    default: break;
  }
  return false;
}

// 0 .. 100: how much of the hold is done (100 when the long press comes)
static uint8_t _holdFill(uint8_t button) {
  if(!s_holding[button])
    return 0;
  const uint32_t held = Time_getMillis() - s_holdStart[button];
  return (held >= HOLD_MS) ? 100 : (uint8_t)((held * 100u) / HOLD_MS);
}

// A small button: the frame of GUI_DRAW_BTN, 9 pixels high, text in font3x5. `fill` (0 .. 100)
// inverts the button from the left: the hold animation. 100 = the look of a pressed button.
static void _drawButton(int16_t x, int16_t w, const char* text, uint8_t fill) {
  const int16_t y = BUTTON_Y;
  Gfx_drawVLine(x, y + 1, BUTTON_H - 2, 1);
  Gfx_drawVLine(x + w - 1, y + 1, BUTTON_H - 2, 1);
  Gfx_drawHLine(x + 1, y, w - 2, 1);
  Gfx_drawHLine(x + 1, y + BUTTON_H - 1, w - 2, 1);
  Gfx_drawStringAligned(x + w / 2, y + 2, text, &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);

  const int16_t width = (int16_t)(((w - 2) * fill + 50) / 100);
  if(width > 0)
    Gfx_invertRect(x + 1, y + 1, width, BUTTON_H - 2);
}

static void _draw(void) {
  char str[32];

  if(s_state == APP_SYNC_STATE__INSTALLED) {
    GUI_DRAW_TITLE((SCREEN_W - 13 * font5x5.x_advance) / 2, 1, 13, "APP INSTALLED");
    Gfx_drawStringAligned(SCREEN_W / 2, 11, s_name, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    // Each button over its round button: button 1 is the left one, button 2 the right one
    _drawButton(BUTTON_MARGIN, 40, "SYNC MORE", 0);
    _drawButton(SCREEN_W - BUTTON_MARGIN - 22, 22, "OPEN", 0);
    return;
  }

  GUI_DRAW_TITLE((SCREEN_W - 8 * font5x5.x_advance) / 2, 1, 8, "APP SYNC");

  switch(s_state) {
    case APP_SYNC_STATE__RECEIVING: {
      const uint32_t progress = (s_size > 0) ? (s_received * 100u) / s_size : 0;
      Gfx_drawStringAligned(SCREEN_W / 2, 12, "Receiving", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      snprintf(str, sizeof(str), "%u%%", (unsigned)progress);
      Gfx_drawStringAligned(SCREEN_W / 2, 23, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      Gfx_invertRect(14, 22, (int16_t)progress, 7);
      Gfx_drawRoundRect(13, 21, SCREEN_W - 26, 9, 3, 1);
    } break;

    case APP_SYNC_STATE__WAITING: {
      snprintf(str, sizeof(str), "INSTALL %s?", s_name);
      const gfx_font_t* font = (Gfx_getStringWidth(str, &font5x5) > SCREEN_W - 4) ? &font3x5 : &font5x5;
      Gfx_drawStringAligned(SCREEN_W / 2, 11, str, font, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      // NO over button 1 (left), YES over button 2 (right). They fill while they are held.
      _drawButton(BUTTON_MARGIN, 22, "NO", _holdFill(0));
      _drawButton(SCREEN_W - BUTTON_MARGIN - 22, 22, "YES", _holdFill(1));
    } break;

    case APP_SYNC_STATE__REFUSED:
    case APP_SYNC_STATE__TIMEOUT:
      Gfx_drawStringAligned(SCREEN_W / 2, 12, "REFUSED", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      GUI_DRAW_BTN(12, 20, 42, 11, "BACK", s_cancel == 1);
      break;

    case APP_SYNC_STATE__IDLE:
    default:
      if(s_failed) {
        Gfx_drawStringAligned(SCREEN_W / 2, 12, "NOT INSTALLED", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      } else {
        snprintf(str, sizeof(str), "Waiting%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
        Gfx_drawStringAligned((SCREEN_W - 7 * font5x5.x_advance) / 2, 12, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      }
      GUI_DRAW_BTN(12, 20, 42, 11, "BACK", s_cancel == 1);
      break;
  }
}
