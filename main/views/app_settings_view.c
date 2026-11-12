/***************************************************************************************************
 * @file    : app_settings_view.c
 * @brief   : The settings of one installed app, in the look of the Settings view.
 *
 * Rows with the setting name and its value. Button 1 / 2 = previous / next row. Long press 1 / 2
 * = previous / next value; the value is saved at once (flash key s<id>, apps.c). Both buttons go
 * back to the app menu with this app selected (main.c, launcher_view.c).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_settings_view.h"

#include <stdio.h>
#include <string.h>

#include "view.h"
#include "app.h"
#include "app_view.h"
#include "apps.h"
#include "gfx.h"
#include "font.h"

/* > Defines **************************************************************************************/
#define SCREEN_W        128
#define LABEL_X         (SCREEN_W / 2 + 3)    // Right end of the name (as in the Settings view)
#define VALUE_X         (SCREEN_W / 2 + 6)    // Start of the value
#define LABEL_SIZE      (STAPP_SETTING_NAME_LEN + 2)   // Name + ':' + 0
#define VALUE_SIZE      (STAPP_VALUE_LEN + 1)

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _change(int8_t step);
static void _drawRow(int16_t y, uint8_t row, bool selected);

/* > Local Variables ******************************************************************************/
view_t g_appSettingsView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
};

// A copy of the texts, so the view does not need the package while it is open
static uint32_t s_id = APPS_ID__NONE;
static uint8_t  s_count = 0;
static uint8_t  s_row = 0;
static uint8_t  s_valueCount[STAPP_MAX_SETTINGS];
static char     s_labels[STAPP_MAX_SETTINGS][LABEL_SIZE];
static char     s_values[STAPP_MAX_SETTINGS][STAPP_MAX_VALUES][VALUE_SIZE];

/* > Function Definitions *************************************************************************/

bool AppSettings_setApp(uint32_t id) {
  stapp_info_t info;
  uint8_t* package = Apps_getWorkBuffer();

  if(Apps_getSettingCount(id) == 0 || Apps_loadPackage(id, package, &info) != STAPP_OK || info.settingCount == 0)
    return false;

  s_id = id;
  s_row = 0;
  s_count = info.settingCount;
  for(uint8_t s = 0; s < s_count; s++) {
    const char* name = NULL;
    uint8_t count = 0, def = 0;
    if(!Stapp_setting(package, &info, s, &name, &count, &def))
      return false;

    snprintf(s_labels[s], sizeof(s_labels[s]), "%.*s:", STAPP_SETTING_NAME_LEN, name);
    s_valueCount[s] = count;
    for(uint8_t v = 0; v < count; v++) {
      const char* value = Stapp_settingValue(package, &info, s, v);
      snprintf(s_values[s][v], sizeof(s_values[s][v]), "%.*s", STAPP_VALUE_LEN, value ? value : "");
    }
  }
  return true;
}

uint32_t AppSettings_getAppId(void) {
  return s_id;
}

uint8_t AppSettings_getRow(void) {
  return s_row;
}

/* > Local Function Definitions *******************************************************************/

static bool _eventHandler(uint16_t event) {
  switch(event) {
    case EVENT__PRESS_1:
      if(s_row > 0) {
        s_row--;
        App_click(1000);
      }
      return true;

    case EVENT__PRESS_2:
      if(s_row + 1 < s_count) {
        s_row++;
        App_click(1000);
      }
      return true;

    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_HOLD_1:
      _change(-1);
      return true;

    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_2:
      _change(+1);
      return true;

    default: break;
  }
  return false;
}

// The next or previous value (it wraps around), saved at once
static void _change(int8_t step) {
  if(s_row >= s_count || s_valueCount[s_row] == 0)
    return;

  const uint8_t count = s_valueCount[s_row];
  const uint8_t value = Apps_getSetting(s_id, s_row);
  const uint8_t next = (uint8_t)((value + count + step) % count);
  if(Apps_setSetting(s_id, s_row, next) == APPS_OK)
    App_click(1200);
}

static void _drawRow(int16_t y, uint8_t row, bool selected) {
  const uint8_t value = Apps_getSetting(s_id, row);
  const char* valueText = (value < s_valueCount[row]) ? s_values[row][value] : "?";
  const gfx_color_t color = selected ? 0 : 1;

  if(selected) {
    // A long name does not fit in the 5x5 font: the small font then
    const gfx_font_t* font = (Gfx_getStringWidth(s_labels[row], &font5x5) > LABEL_X) ? &font3x5 : &font5x5;
    Gfx_drawStringAligned(LABEL_X, y, s_labels[row], font, color, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(VALUE_X, y, valueText, &font5x5, color, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  } else {
    Gfx_drawStringAligned(LABEL_X - 3, y, s_labels[row], &font3x5, color, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(VALUE_X, y, valueText, &font3x5, color, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
}

static void _draw(void) {
  if(s_count == 0)
    return;
  if(s_row >= s_count)
    s_row = 0;

  // The previous row above, the selected row in the light band, the next row below
  if(s_row > 0)
    _drawRow(2, s_row - 1, false);

  Gfx_fillRect(0, 9, SCREEN_W, 13, 1);
  _drawRow(13, s_row, true);

  if(s_row + 1 < s_count)
    _drawRow(24, s_row + 1, false);
}
