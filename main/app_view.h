#ifndef _APP_VIEW_H
#define _APP_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "view.h"
#include "gfx.h"
#include "font.h"

/* > Macros ***************************************************************************************/

#define GUI_DRAW_BTN(x, y, w, h, str, select)   do {  \
  Gfx_drawVLine(x, y+1, h-2, 1);            \
  Gfx_drawVLine(x+w-1, y+1, h-2, 1);        \
  Gfx_drawHLine(x+1, y, w-2, 1);            \
  Gfx_drawHLine(x+1, y+h-1, w-2, 1);        \
  Gfx_drawStringAligned(x + w/2, y+(h-5)/2, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0);  \
  if(select) Gfx_invertRect(x+1, y+1, w-2, h-2); \
} while(0)

#define GUI_DRAW_ROUND_RECT(x, y, w, h, filled)   do {  \
  Gfx_drawVLine(x, y+1, h-2, 1);            \
  Gfx_drawVLine(x+w-1, y+1, h-2, 1);        \
  Gfx_drawHLine(x+1, y, w-2, 1);            \
  Gfx_drawHLine(x+1, y+h-1, w-2, 1);        \
  if(filled) Gfx_fillRect(x+1, y+1, w-2, h-2, 1); \
} while(0)

#define GUI_DRAW_TITLE(x, y, len, str) do { \
Gfx_drawString(x, y, str, &font5x5, 1, 1, GFX_ROTATION__0); \
Gfx_drawVLine(x - 2, y, font5x5.height, 1);  \
Gfx_drawVLine(x + (len * font5x5.x_advance), y, font5x5.height, 1);  \
} while(0)

// A title in plain letters (few lit pixels), centred on x: the Bluetooth screens. The current of
// the display adds to the current peak of the radio start (docs/BLUETOOTH.md, "Radio start").
#define GUI_DRAW_TITLE_THIN(cx, y, str) \
  Gfx_drawStringAligned(cx, y, str, &font5x5, 1, 0, GFX_ALIGN__CENTER, GFX_ROTATION__0)


/* > Definitions **********************************************************************************/
// VIEW(_id, _name)
#define VIEW_LIST \
  VIEW(BLANK, blank) \
  VIEW(INTRO, intro) \
  VIEW(TOTAL, total) \
  VIEW(KILL_DEAD, killDead) \
  VIEW(STATS, stats) \
  VIEW(SETTINGS, settings) \
  VIEW(LAUNCHER, launcher) \
  VIEW(APP_HOST, appHost) \
  VIEW(APP_SETTINGS, appSettings) \
  VIEW(APP_SYNC, appSync) \
  VIEW(BLUETOOTH, bluetooth) \
  VIEW(ASK, ask) \
  VIEW(UPDATE, update) \
  VIEW(FW_ACCEPT, fwAccept) \
  VIEW(MSG, msg) \
  VIEW(AUTO_OFF, autoOff) \
  VIEW(BATTERY_LOW, batteryLow) \
  VIEW(BATTERY_CRITICAL, batteryCritical)


/* > Enumerations *********************************************************************************/
// Where the new view comes from when the view changes
typedef enum app_view_from_e {
  APP_VIEW_FROM__TOP = 0,   // The normal change: the new view comes down from above
  APP_VIEW_FROM__BOTTOM,    // The new view comes up from below (used to open the app menu)
} app_view_from_t;

typedef enum view_id_e {
  #define VIEW(_id, _name) VIEW_ID__##_id,
    VIEW_LIST
  #undef VIEW

  VIEW_COUNT,
} view_id_t;


/* > External variables ***************************************************************************/
#define VIEW(_id, _name) extern view_t g_##_name##View;
  VIEW_LIST
#undef VIEW


/* > Function Declarations *************************************************************************/
void      AppView_update(void);
void      AppView_show(void);
bool      AppView_eventHandler(uint16_t event);
void      AppView_set(view_id_t viewId);
void      AppView_setFrom(view_id_t viewId, app_view_from_t from, uint32_t durationMs);
view_id_t AppView_get(void);
view_id_t AppView_getNext(void);
bool      AppView_isActive(view_id_t viewId); // Current or pending view
view_id_t AppView_getPrevious(void);

#endif // _APP_VIEW_H