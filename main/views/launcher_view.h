/***************************************************************************************************
 * @file    : launcher_view.h
 * @brief   : The app menu: both buttons in an app open it, both buttons or 3 s without a press
 *            open the selected app. Button 1 = previous app, button 2 = next app.
 **************************************************************************************************/

#ifndef _LAUNCHER_VIEW_H
#define _LAUNCHER_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "app_view.h"

/* > Defines **************************************************************************************/
#define LAUNCHER_SELECT_MS    3000  // The selected app opens after this time without a press

/* > Function Declaration *************************************************************************/

// The "both buttons" short press (main.c). In an app: open the menu with this app selected.
// In the menu: open the selected app.
void      Launcher_onBothButtons(void);

// Each loop (app.c): opens the selected app after LAUNCHER_SELECT_MS without a press
void      Launcher_update(uint32_t now);

// The view of the selected app (for the simulator and the tests)
view_id_t Launcher_getSelectedView(void);

#endif // _LAUNCHER_VIEW_H
