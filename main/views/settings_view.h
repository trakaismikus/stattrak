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

#endif // _SETTINGS_VIEW_H
