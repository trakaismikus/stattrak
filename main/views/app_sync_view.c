/***************************************************************************************************
 * @file    : app_sync_view.c
 * @brief   : Settings > "Apps: SYNC". See app_sync_view.h and docs/APPS.md, section 6.
 *
 * Bluetooth is on (Ota_init) and the device refuses new firmware while this view is open
 * (Ota_setUpdateAllowed(false)): nobody near the person can send a firmware here. The update
 * view calls Ota_init() again, and that allows firmware again.
 *
 * Buttons: long press 1 = back (or "no" to the install question), hold button 2 = "yes".
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_sync_view.h"

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "apps.h"
#include "gfx.h"
#include "font.h"
#include "time.h"
#include "ota.h"
#include "esp_log.h"

/* > Defines **************************************************************************************/
#define SCREEN_W    128

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);
static void _reset(void);
static void _answer(bool yes);

/* > Local Variables ******************************************************************************/
static const char* TAG = "AppSync";

view_t g_appSyncView = {
  .init = _init,
  .deinit = _deinit,
  .draw = _draw,
  .eventHandler = _eventHandler,
};

static app_sync_state_t s_state = APP_SYNC_STATE__IDLE;
static uint32_t     s_id = 0;
static uint32_t     s_size = 0;
static uint32_t     s_crc = 0;
static uint32_t     s_received = 0;     // Bytes in the buffer = the next expected offset
static uint32_t     s_questionStart = 0;
static uint32_t     s_lastDataMs = 0;     // A transfer that stops (page closed, out of range) ends after APP_SYNC_DATA_MS
static stapp_info_t s_info;
static char         s_name[STAPP_MAX_NAME + 1];
static uint8_t      s_cancel = 0;

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
  if(size < STAPP_HANDLERS_OFFSET + 2 * STAPP_EVENT_COUNT + 4)
    return APP_SYNC_ERR_DATA;   // Smaller than the smallest package

  s_id = id;
  s_size = size;
  s_crc = crc;
  s_received = 0;
  s_lastDataMs = Time_getMillis();
  s_state = APP_SYNC_STATE__RECEIVING;
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

  // A failed check ends this install: the page starts again with BEGIN
  if(Stapp_crc32(buffer, s_size) != s_crc) {
    s_state = APP_SYNC_STATE__IDLE;
    return APP_SYNC_ERR_CRC;
  }

  stapp_result_t check = Stapp_check(buffer, s_size, &s_info);
  if(check == STAPP_OK && s_info.id != s_id)
    check = STAPP_ERR_HEADER;
  if(check != STAPP_OK) {
    ESP_LOGW(TAG, "Package refused: %s", Stapp_resultText(check));
    if(reason)
      *reason = check;
    s_state = APP_SYNC_STATE__IDLE;
    return APP_SYNC_ERR_PACKAGE;
  }

  snprintf(s_name, sizeof(s_name), "%s", s_info.name);
  s_questionStart = Time_getMillis();
  s_state = APP_SYNC_STATE__WAITING;
  App_click(1000);
  return APP_SYNC_OK;
}

void AppSync_update(uint32_t now) {
  if(s_state == APP_SYNC_STATE__WAITING && (now - s_questionStart) >= APP_SYNC_QUESTION_MS)
    s_state = APP_SYNC_STATE__TIMEOUT;

  // The page stopped sending: wait for a new install
  if(s_state == APP_SYNC_STATE__RECEIVING && (now - s_lastDataMs) >= APP_SYNC_DATA_MS)
    _reset();
}

/* > Local Function Definitions *******************************************************************/

static void _init(void) {
  _reset();
  s_cancel = 0;
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
  s_received = 0;
  s_size = 0;
}

static void _answer(bool yes) {
  if(s_state != APP_SYNC_STATE__WAITING)
    return;

  if(!yes) {
    s_state = APP_SYNC_STATE__REFUSED;
    App_click(800);
    return;
  }

  // The one flash write of the package
  const apps_result_t result = Apps_install(Apps_getWorkBuffer(), &s_info);
  s_state = (result == APPS_OK) ? APP_SYNC_STATE__INSTALLED : APP_SYNC_STATE__REFUSED;
  if(result != APPS_OK)
    ESP_LOGE(TAG, "Install failed: %d", (int)result);
  App_click(1200);
}

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
      return true;

    case EVENT__LONG_PRESS_1:
      if(s_state == APP_SYNC_STATE__WAITING)
        _answer(false);
      else if(s_state == APP_SYNC_STATE__RECEIVING)
        _reset();   // Stop this transfer; the page must start again with BEGIN
      else
        AppView_set(AppView_getPrevious());
      return true;

    case EVENT__LONG_PRESS_2:
      _answer(true);
      return true;

    case EVENT__RAW_PRESS_1:
      s_cancel = 1;
      break;

    case EVENT__RAW_RELEASE_1:
      s_cancel = 0;
      break;

    default: break;
  }
  return false;
}

static void _draw(void) {
  char str[32];

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
      Gfx_drawStringAligned(SCREEN_W / 2, 23, "HOLD 2: YES  HOLD 1: NO", &font3x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    } break;

    case APP_SYNC_STATE__INSTALLED:
      Gfx_drawStringAligned(SCREEN_W / 2, 12, "INSTALLED", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      Gfx_drawStringAligned(SCREEN_W / 2, 22, s_name, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      break;

    case APP_SYNC_STATE__REFUSED:
    case APP_SYNC_STATE__TIMEOUT:
      Gfx_drawStringAligned(SCREEN_W / 2, 12, "REFUSED", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      GUI_DRAW_BTN(12, 20, 42, 11, "BACK", s_cancel == 1);
      break;

    case APP_SYNC_STATE__IDLE:
    default:
      snprintf(str, sizeof(str), "Waiting%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
      Gfx_drawStringAligned((SCREEN_W - 7 * font5x5.x_advance) / 2, 12, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      GUI_DRAW_BTN(12, 20, 42, 11, "BACK", s_cancel == 1);
      break;
  }
}
