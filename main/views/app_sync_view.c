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
 * The buttons on the display answer a hold (main/views/hold_button.h): only a hold that starts
 * while the screen shows counts, and the button fills while it is held.
 * After the install: hold 1 = SYNC MORE (wait for the next app), hold 2 = OPEN (the app opens,
 * and Bluetooth goes off with this view). Hold 1 at other times = BACK.
 *
 * The same steps carry the transfers of docs/DATA.md (XFER_BEGIN): a restore (the device asks
 * "RESTORE BACKUP?" first), a start picture and a preview frame (no question). DATA_CLEAR asks
 * "DELETE <NAME> DATA?". A preview frame shows until a button is pressed or PREVIEW_STOP.
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
#include "hold_button.h"
#include "bt_start.h"
#include "esp_log.h"
#include "app_data.h"
#include "backup.h"
#include "start_pic.h"

/* > Defines **************************************************************************************/
#define SCREEN_W        128

/* > Local Function Declarations ******************************************************************/
static void _init(void);
static void _deinit(void);
static void _draw(void);
static bool _eventHandler(uint16_t event);
static void _reset(void);
static void _answer(bool yes);
static app_sync_result_t _endTransfer(stapp_result_t* reason);
static app_sync_result_t _endPreview(void);
static void _checkScreen(void);

/* > Local Variables ******************************************************************************/
static const char* TAG = "AppSync";

view_t g_appSyncView = {
  .init = _init,
  .deinit = _deinit,
  .draw = _draw,
  .eventHandler = _eventHandler,
};

static app_sync_state_t s_state = APP_SYNC_STATE__IDLE;
static app_sync_kind_t  s_kind = APP_SYNC_KIND__PACKAGE;   // What the transfer or the question is for
static app_sync_kind_t  s_failedKind = APP_SYNC_KIND__PACKAGE;
static uint32_t     s_doneMs = 0;         // A start picture shows for 2 s after it arrived
static uint8_t      s_restoreWritten = 0;
static uint8_t      s_restoreSkipped = 0;
// The preview frame that shows (the next one comes into the work buffer meanwhile)
static uint8_t      s_preview[START_PIC_SIZE];
static bool         s_previewShown = false;
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
// The buttons on the display. A new screen (install state) resets them: only a hold that starts
// while a screen shows answers it.
static hold_buttons_t s_hold;
static int16_t      s_shownScreen = -1;

/* > Function Definitions *************************************************************************/

bool AppSync_isActive(void) {
  return AppView_get() == VIEW_ID__APP_SYNC && AppView_getNext() == VIEW_ID__APP_SYNC;
}

app_sync_state_t AppSync_getState(void) {
  return s_state;
}

app_sync_kind_t AppSync_getKind(void) {
  return s_kind;
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
  s_previewShown = false;
  s_kind = APP_SYNC_KIND__PACKAGE;
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
  if(!s_begun || s_kind != APP_SYNC_KIND__PACKAGE || s_state == APP_SYNC_STATE__RECEIVING ||
     !Stapp_isNameField(name, length))
    return APP_SYNC_ERR_DATA;

  memcpy(s_name, name, STAPP_MAX_NAME);
  s_name[STAPP_MAX_NAME] = '\0';
  s_begun = false;
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

  // A preview frame: the parts in any order (the page sends only the parts that are not 0)
  if(s_kind == APP_SYNC_KIND__PREVIEW) {
    if(offset >= s_size)
      return APP_SYNC_ERR_DATA;
    uint32_t count = length;
    if(count > s_size - offset)
      count = s_size - offset;
    memcpy(&Apps_getWorkBuffer()[offset], data, count);
    s_lastDataMs = Time_getMillis();
    if(expected)
      *expected = (uint16_t)(offset + count);
    return APP_SYNC_OK;
  }

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
  if(s_kind == APP_SYNC_KIND__PREVIEW)
    return _endPreview();
  if(s_received != s_size)
    return APP_SYNC_ERR_OFFSET;
  if(s_kind == APP_SYNC_KIND__RESTORE || s_kind == APP_SYNC_KIND__START_PIC)
    return _endTransfer(reason);

  // A failed check ends this install: the page starts again with BEGIN (and the question)
  if(Stapp_crc32(buffer, s_size) != s_crc) {
    _reset();
    s_failed = true;
    s_failedKind = APP_SYNC_KIND__PACKAGE;
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
    s_failedKind = APP_SYNC_KIND__PACKAGE;
    return APP_SYNC_ERR_PACKAGE;
  }

  // The one flash write of the package
  const apps_result_t result = Apps_install(buffer, &s_info);
  if(result != APPS_OK) {
    ESP_LOGE(TAG, "Install failed: %d", (int)result);
    _reset();
    s_failed = true;
    s_failedKind = APP_SYNC_KIND__PACKAGE;
    return APP_SYNC_ERR_FLASH;
  }

  s_state = APP_SYNC_STATE__INSTALLED;
  App_click(1200);
  return APP_SYNC_OK;
}

app_sync_result_t AppSync_xferBegin(app_sync_kind_t kind, uint32_t size, uint32_t crc) {
  if(s_state == APP_SYNC_STATE__WAITING)
    return APP_SYNC_ERR_WAITING;
  const bool picture = (kind == APP_SYNC_KIND__START_PIC || kind == APP_SYNC_KIND__PREVIEW);
  if((kind == APP_SYNC_KIND__RESTORE && (size < BACKUP_MIN_SIZE || size > STAPP_MAX_SIZE)) ||
     (picture && size != START_PIC_SIZE) || (kind != APP_SYNC_KIND__RESTORE && !picture))
    return APP_SYNC_ERR_DATA;

  _reset();
  s_failed = false;
  s_kind = kind;
  s_size = size;
  s_crc = crc;
  if(kind == APP_SYNC_KIND__RESTORE) {
    // Nothing comes before the person says YES to "RESTORE BACKUP?"
    s_previewShown = false;
    s_questionStart = Time_getMillis();
    s_state = APP_SYNC_STATE__WAITING;
    App_click(1000);
    return APP_SYNC_OK;
  }
  if(kind == APP_SYNC_KIND__START_PIC)
    s_previewShown = false;
  if(kind == APP_SYNC_KIND__PREVIEW)
    memset(Apps_getWorkBuffer(), 0, START_PIC_SIZE);   // Parts that do not come are 0
  s_lastDataMs = Time_getMillis();
  s_state = APP_SYNC_STATE__RECEIVING;
  return APP_SYNC_OK;
}

app_sync_result_t AppSync_clearData(uint32_t id) {
  uint16_t rows = 0;
  if(s_state == APP_SYNC_STATE__WAITING)
    return APP_SYNC_ERR_WAITING;
  if(!AppData_info(id, NULL, &rows) || rows == 0)
    return APP_SYNC_ERR_DATA;

  _reset();
  s_failed = false;
  s_previewShown = false;
  s_kind = APP_SYNC_KIND__CLEAR;
  s_id = id;
  snprintf(s_name, sizeof(s_name), "%s", Apps_getName(id) ? Apps_getName(id) : "");
  s_questionStart = Time_getMillis();
  s_state = APP_SYNC_STATE__WAITING;
  App_click(1000);
  return APP_SYNC_OK;
}

void AppSync_previewStop(void) {
  if(s_kind == APP_SYNC_KIND__PREVIEW || s_state == APP_SYNC_STATE__PREVIEW) {
    _reset();
    s_failed = false;
  }
  s_previewShown = false;
}

void AppSync_getRestoreCounts(uint8_t* written, uint8_t* skipped) {
  if(written)
    *written = s_restoreWritten;
  if(skipped)
    *skipped = s_restoreSkipped;
}

void AppSync_update(uint32_t now) {
  // Signed: a Bluetooth command can set the start time a little after `now` was read
  if(s_state == APP_SYNC_STATE__WAITING && (int32_t)(now - s_questionStart) >= APP_SYNC_QUESTION_MS)
    s_state = APP_SYNC_STATE__TIMEOUT;

  // The page stopped sending: wait for a new install
  if(s_state == APP_SYNC_STATE__RECEIVING && (int32_t)(now - s_lastDataMs) >= APP_SYNC_DATA_MS) {
    const app_sync_kind_t kind = s_kind;
    _reset();
    if(kind == APP_SYNC_KIND__PREVIEW && s_previewShown) {
      s_kind = APP_SYNC_KIND__PREVIEW;
      s_state = APP_SYNC_STATE__PREVIEW;    // The last frame stays
    } else if(kind != APP_SYNC_KIND__PREVIEW) {
      s_failed = true;
      s_failedKind = kind;
    }
  }

  // A new start picture shows for 2 s, then "Waiting..." again
  if(s_state == APP_SYNC_STATE__INSTALLED && s_kind == APP_SYNC_KIND__START_PIC &&
     (int32_t)(now - s_doneMs) >= START_PIC_SHOW_MS)
    _reset();
}

/* > Local Function Definitions *******************************************************************/

// END of a restore or a start picture: the CRC, then the one write
static app_sync_result_t _endTransfer(stapp_result_t* reason) {
  const uint8_t* buffer = Apps_getWorkBuffer();
  const app_sync_kind_t kind = s_kind;

  if(Stapp_crc32(buffer, s_size) != s_crc) {
    _reset();
    s_failed = true;
    s_failedKind = kind;
    return APP_SYNC_ERR_CRC;
  }

  if(kind == APP_SYNC_KIND__RESTORE) {
    const backup_result_t result = Backup_restore(buffer, s_size, &s_restoreWritten, &s_restoreSkipped);
    if(result != BACKUP_OK) {
      if(reason)
        *reason = (stapp_result_t)result;   // The reason byte of status 10 (backup_result_t)
      _reset();
      s_failed = true;
      s_failedKind = kind;
      return APP_SYNC_ERR_PACKAGE;
    }
    s_kind = kind;
    s_state = APP_SYNC_STATE__INSTALLED;
    App_click(1200);
    return APP_SYNC_OK;
  }

  // The start picture
  if(!StartPic_save(buffer)) {
    _reset();
    s_failed = true;
    s_failedKind = kind;
    return APP_SYNC_ERR_FLASH;
  }
  s_kind = kind;
  s_doneMs = Time_getMillis();
  s_state = APP_SYNC_STATE__INSTALLED;
  App_click(1200);
  return APP_SYNC_OK;
}

// END of a preview frame: shown at once, nothing goes to flash
static app_sync_result_t _endPreview(void) {
  const uint8_t* buffer = Apps_getWorkBuffer();
  if(Stapp_crc32(buffer, START_PIC_SIZE) != s_crc) {
    s_state = s_previewShown ? APP_SYNC_STATE__PREVIEW : APP_SYNC_STATE__IDLE;
    return APP_SYNC_ERR_CRC;
  }
  memcpy(s_preview, buffer, START_PIC_SIZE);
  s_previewShown = true;
  s_state = APP_SYNC_STATE__PREVIEW;
  return APP_SYNC_OK;
}

// The radio starts when the display shows only the Bluetooth sign (bt_start.h)
static void _startBluetooth(void) {
  // "StatTrak | " + the name part that the person chose (docs/APPS.md section 6), for this Bluetooth
  // start only. No part: "StatTrak". The update view and the accept view are always "StatTrak".
  char part[APPS_DEVICE_NAME_MAX + 1];
  char name[APP_BLE_NAME_MAX + 1] = "";
  Apps_getDeviceName(part);
  if(part[0] != '\0')
    snprintf(name, sizeof(name), "%s%s", APPS_DEVICE_NAME_PREFIX, part);
  AppBle_setNextName(name);

  Ota_init();
  // Ota_init() can return before AppBle_init() used the name (Bluetooth on already, no memory):
  // forget it here too, so that the next Bluetooth start (the update view) is "StatTrak"
  AppBle_setNextName(NULL);
  // A view of a user feature: nobody can send a firmware while it is open (docs/BLUETOOTH.md)
  Ota_setUpdateAllowed(false);
}

static void _init(void) {
  _reset();
  s_failed = false;
  s_previewShown = false;
  HoldButtons_reset(&s_hold);
  s_shownScreen = -1;
  BtStart_begin(VIEW_ID__APP_SYNC, _startBluetooth);
}

static void _deinit(void) {
  BtStart_cancel(VIEW_ID__APP_SYNC);
  Ota_deinit();
  _reset();
}

static void _reset(void) {
  s_state = APP_SYNC_STATE__IDLE;
  s_kind = APP_SYNC_KIND__PACKAGE;
  s_begun = false;
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

  // "DELETE <NAME> DATA?": done at once
  if(s_kind == APP_SYNC_KIND__CLEAR) {
    AppData_erase(s_id);
    s_state = APP_SYNC_STATE__INSTALLED;
    App_click(1200);
    return;
  }

  // Yes: now the page sends the data
  s_received = 0;
  s_lastDataMs = Time_getMillis();
  s_state = APP_SYNC_STATE__RECEIVING;
  App_click(1200);
}

// A new screen (an other install state, or "NOT INSTALLED"): holds that started before do not count
static void _checkScreen(void) {
  const int16_t screen = (int16_t)(((s_state * 8) + s_kind) * 2 + (s_failed ? 1 : 0));
  if(screen != s_shownScreen) {
    s_shownScreen = screen;
    HoldButtons_reset(&s_hold);
  }
}

static bool _eventHandler(uint16_t event) {
  _checkScreen();
  HoldButtons_event(&s_hold, event);

  // A preview frame or a new start picture: a press of a button ends it
  if(s_state == APP_SYNC_STATE__PREVIEW ||
     (s_state == APP_SYNC_STATE__INSTALLED && s_kind == APP_SYNC_KIND__START_PIC) ||
     (s_state == APP_SYNC_STATE__RECEIVING && s_kind == APP_SYNC_KIND__PREVIEW && s_previewShown)) {
    if(event == EVENT__PRESS_1 || event == EVENT__PRESS_2 || event == EVENT__LONG_PRESS_1 || event == EVENT__LONG_PRESS_2) {
      _reset();
      s_previewShown = false;
      HoldButtons_reset(&s_hold);
    }
    return true;
  }

  switch(event) {
    case EVENT__PRESS_1:
    case EVENT__PRESS_2:
      return true;   // The buttons on the display answer a hold only

    case EVENT__LONG_PRESS_1:
      if(!HoldButtons_isHeld(&s_hold, 0))
        return true;
      if(s_state == APP_SYNC_STATE__WAITING) {
        _answer(false);                                     // NO
      } else if(s_state == APP_SYNC_STATE__RECEIVING) {
        const app_sync_kind_t kind = s_kind;
        _reset();   // Stop this transfer; the page must start again with BEGIN
        s_failed = (kind != APP_SYNC_KIND__PREVIEW);
        s_failedKind = kind;
      } else if(s_state == APP_SYNC_STATE__INSTALLED) {
        _reset();                                           // SYNC MORE / OK: wait for the next app
      } else {
        AppView_set(AppView_getPrevious());                 // BACK
      }
      return true;

    case EVENT__LONG_PRESS_2:
      if(!HoldButtons_isHeld(&s_hold, 1))
        return true;
      if(s_state == APP_SYNC_STATE__WAITING) {
        _answer(true);                                      // YES
      } else if(s_state == APP_SYNC_STATE__INSTALLED && s_kind == APP_SYNC_KIND__PACKAGE) {
        // OPEN: as the app menu opens an app. Bluetooth goes off with this view (_deinit).
        AppHost_setApp(s_id);
        AppView_set(VIEW_ID__APP_HOST);
      } else if(s_state == APP_SYNC_STATE__INSTALLED) {
        _reset();                                           // OK (restore, data deleted)
      }
      return true;

    default: break;
  }
  return false;
}

static void _drawScreen(void);

static void _draw(void) {
  if(!BtStart_draw(VIEW_ID__APP_SYNC, _drawScreen))
    _drawScreen();
}

static void _drawScreen(void) {
  char str[32];

  _checkScreen();

  // A preview frame (also while the next one comes) and a new start picture: the full display
  if(s_state == APP_SYNC_STATE__PREVIEW || (s_kind == APP_SYNC_KIND__PREVIEW && s_previewShown)) {
    Gfx_drawBitmapRaw(0, 0, s_preview, START_PIC_W, START_PIC_H, 1, GFX_ROTATION__0);
    return;
  }
  if(s_state == APP_SYNC_STATE__INSTALLED && s_kind == APP_SYNC_KIND__START_PIC) {
    const uint8_t* picture = StartPic_get();
    if(picture != NULL)
      Gfx_drawBitmapRaw(0, 0, picture, START_PIC_W, START_PIC_H, 1, GFX_ROTATION__0);
    return;
  }
  if(s_state == APP_SYNC_STATE__INSTALLED && s_kind != APP_SYNC_KIND__PACKAGE) {
    const char* title = (s_kind == APP_SYNC_KIND__RESTORE) ? "BACKUP RESTORED" : "DATA DELETED";
    const int16_t length = (int16_t)strlen(title);
    GUI_DRAW_TITLE((SCREEN_W - length * font5x5.x_advance) / 2, 1, length, title);
    if(s_kind == APP_SYNC_KIND__RESTORE) {
      if(s_restoreSkipped > 0)
        snprintf(str, sizeof(str), "%u SKIPPED", (unsigned)s_restoreSkipped);
      else
        snprintf(str, sizeof(str), "ALL VALUES");
    } else {
      snprintf(str, sizeof(str), "%s", s_name);
    }
    Gfx_drawStringAligned(SCREEN_W / 2, 11, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "OK", HoldButtons_fill(&s_hold, 1));
    return;
  }

  if(s_state == APP_SYNC_STATE__INSTALLED) {
    GUI_DRAW_TITLE((SCREEN_W - 13 * font5x5.x_advance) / 2, 1, 13, "APP INSTALLED");
    Gfx_drawStringAligned(SCREEN_W / 2, 11, s_name, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
    // Each button over its round button: button 1 is the left one, button 2 the right one
    HoldButton_draw(HOLD_BUTTON_MARGIN, 41, "SYNC MORE", HoldButtons_fill(&s_hold, 0));
    HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "OPEN", HoldButtons_fill(&s_hold, 1));
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
      if(s_kind == APP_SYNC_KIND__RESTORE)
        snprintf(str, sizeof(str), "RESTORE BACKUP?");
      else if(s_kind == APP_SYNC_KIND__CLEAR)
        snprintf(str, sizeof(str), "DELETE %s DATA?", s_name);
      else
        snprintf(str, sizeof(str), "INSTALL %s?", s_name);
      const gfx_font_t* font = (Gfx_getStringWidth(str, &font5x5) > SCREEN_W - 4) ? &font3x5 : &font5x5;
      Gfx_drawStringAligned(SCREEN_W / 2, 11, str, font, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      // NO over button 1 (left), YES over button 2 (right). They fill while they are held.
      HoldButton_draw(HOLD_BUTTON_MARGIN, HOLD_BUTTON_W, "NO", HoldButtons_fill(&s_hold, 0));
      HoldButton_draw(HOLD_BUTTON_RIGHT_X(HOLD_BUTTON_W), HOLD_BUTTON_W, "YES", HoldButtons_fill(&s_hold, 1));
    } break;

    case APP_SYNC_STATE__REFUSED:
    case APP_SYNC_STATE__TIMEOUT:
      Gfx_drawStringAligned(SCREEN_W / 2, 12, "REFUSED", &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      HoldButton_draw(HOLD_BUTTON_MARGIN, HOLD_BUTTON_W, "BACK", HoldButtons_fill(&s_hold, 0));
      break;

    case APP_SYNC_STATE__IDLE:
    default:
      if(s_failed) {
        const char* text = (s_failedKind == APP_SYNC_KIND__RESTORE) ? "NOT RESTORED" :
                           (s_failedKind == APP_SYNC_KIND__START_PIC) ? "NOT SAVED" : "NOT INSTALLED";
        Gfx_drawStringAligned(SCREEN_W / 2, 12, text, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);
      } else {
        snprintf(str, sizeof(str), "Waiting%.*s", (int)((Time_getMillis() >> 9) & 3), "...");
        Gfx_drawStringAligned((SCREEN_W - 7 * font5x5.x_advance) / 2, 12, str, &font5x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      }
      HoldButton_draw(HOLD_BUTTON_MARGIN, HOLD_BUTTON_W, "BACK", HoldButtons_fill(&s_hold, 0));
      break;
  }
}
