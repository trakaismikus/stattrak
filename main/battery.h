/***************************************************************************************************
 * @file    : battery.h
 * @brief   : Battery driver header file
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

#ifndef _BATTERY_H
#define _BATTERY_H

/* > Includes *************************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/* > Enumerations *********************************************************************************/

enum {
  BATTERY_TYPE__UNKNOWN = 0,
  BATTERY_TYPE__USB,
  BATTERY_TYPE__RCR123A,
  BATTERY_TYPE__LIPO_1S,
  BATTERY_TYPE__LIPO_2S,
  BATTERY_TYPE__LIPO_3S,
  BATTERY_TYPE__NIMH_3S,
  BATTERY_TYPE__NIMH_8S,
};

/* > Function Declarations ************************************************************************/

void      Battery_init(void);
uint8_t   Battery_detect(void);
void      Battery_read(void);
uint8_t   Battery_readLevel(void);
uint16_t  Battery_readVoltage(void);

bool      Battery_isCritical(void);

uint8_t   Battery_getType(void);
void      Battery_setType(uint8_t type);

uint8_t   Battery_getTypeUsed(void);

#endif // _BATTERY_H