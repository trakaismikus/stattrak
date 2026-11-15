/***************************************************************************************************
 * @file    : app_host_view.h
 * @brief   : The view that runs the installed apps with the app engine (components/stapp), and
 *            the background app of Settings > Background (docs/APPS.md section 4.6).
 **************************************************************************************************/

#ifndef _APP_HOST_VIEW_H
#define _APP_HOST_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "stapp.h"

/* > Defines **************************************************************************************/
#define APP_HOST_TICK_MS      100

// AppHost_counterAdded()
#define APP_HOST_COUNTER__KILLS   0
#define APP_HOST_COUNTER__DEATHS  1

/* > Function Declarations ************************************************************************/

// The app that the view runs (the app menu calls it before it opens the view). If the view runs
// an other app now, that app is closed and this one is opened.
void           AppHost_setApp(uint32_t id);

// The id of the app that is open (or that opens next), APPS_ID__NONE if none
uint32_t       AppHost_getAppId(void);

// true while an app is open in the view
bool           AppHost_isOpen(void);

// The app error that the view shows, STAPP_OK if none
stapp_result_t AppHost_getError(void);

// Each loop (App_update): the tick event each 100 ms while the view is the current view, and
// the flash writes that the view must not do while it draws (saved values, error counts).
void           AppHost_update(uint32_t now);

// The device switches off: save the values of the open app now
void           AppHost_onShutdown(void);

// true while an app with the permission "awake" is the open view: auto-off waits (docs/DATA.md)
bool           AppHost_keepsAwake(void);

// --- The background app (docs/APPS.md section 4.6)

// The game kills or deaths went up by a built-in counter screen or an app: the background app
// gets the kill / death event in the next AppHost_update() (not the app that added them)
void           AppHost_counterAdded(uint8_t counter);

// Settings > Background changed, or the apps changed: start / stop the background app now (never
// in a draw function). App_init calls it too.
void           AppHost_backgroundRefresh(void);

// Apps: SYNC and the update mode: true at the open of the view (from a button event; the
// background app stops now), false at the close (it starts again in the next AppHost_update())
void           AppHost_backgroundPause(bool paused);

// Factory reset: the flash is erased. The background app is forgotten (no close event, no writes).
void           AppHost_backgroundForget(void);

// The app that runs in the background now (APPS_ID__NONE = none), and its error (STAPP_OK = none)
uint32_t       AppHost_getBackgroundId(void);
stapp_result_t AppHost_getBackgroundError(void);

#endif // _APP_HOST_VIEW_H
