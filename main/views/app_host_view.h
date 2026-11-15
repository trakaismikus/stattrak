/***************************************************************************************************
 * @file    : app_host_view.h
 * @brief   : The view that runs one installed app with the app engine (components/stapp).
 **************************************************************************************************/

#ifndef _APP_HOST_VIEW_H
#define _APP_HOST_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "stapp.h"

/* > Defines **************************************************************************************/
#define APP_HOST_TICK_MS      100

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

#endif // _APP_HOST_VIEW_H
