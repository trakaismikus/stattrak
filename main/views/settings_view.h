/***************************************************************************************************
 * @file    : settings_view.h
 * @brief   : Settings View header file
 * @author  : Janis Zaharans
 * @date    : Aug 1, 2022
 **************************************************************************************************/

#ifndef _SETTINGS_VIEW_H
#define _SETTINGS_VIEW_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/

/* > Enumerations *********************************************************************************/

/* > Type definitions *****************************************************************************/

/* > Function Declaration *************************************************************************/

void SettingsView_init(void);
void SettingsView_setBatteryType(uint8_t type);
uint8_t SettingsView_getSelectedId(void);   // The setting at the cursor (setting_id_t)
// Settings > Background: the values again from the installed apps (the row comes or goes), and the
// app of a value (APPS_ID__NONE for OFF)
void SettingsView_refreshBackground(void);
uint32_t SettingsView_getBackgroundApp(uint32_t value);

#endif // _SETTINGS_VIEW_H
