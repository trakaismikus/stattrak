/***************************************************************************************************
 * @file    : app_settings_view.h
 * @brief   : The settings of one installed app (hold button 2 on its title picture in the app menu).
 **************************************************************************************************/

#ifndef _APP_SETTINGS_VIEW_H
#define _APP_SETTINGS_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Function Declarations ************************************************************************/

// Prepares the view for the app `id`. false: the app has no settings (or cannot be read).
bool     AppSettings_setApp(uint32_t id);

// The app of the view (for the app menu: both buttons go back with this app selected)
uint32_t AppSettings_getAppId(void);

// The row that is selected (for the tests)
uint8_t  AppSettings_getRow(void);

#endif // _APP_SETTINGS_VIEW_H
