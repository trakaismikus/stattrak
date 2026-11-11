/***************************************************************************************************
 * @file    : eeprom.h
 * @brief   : EEPROM emulator header file
 * @author  : Janis Zaharans
 * @date    : Apr 8, 2022
 **************************************************************************************************/

 #ifndef _EEPROM_H
 #define _EEPROM_H
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* > Includes *************************************************************************************/
 #include <stdint.h>
 #include <stdbool.h>
 #include "nvs_flash.h"
 
 /* > Defines **************************************************************************************/
 
 /* > Type Definitions *****************************************************************************/
 
typedef struct {
  nvs_handle_t nvsHandle;

  uint32_t* entries;
  uint32_t entryCount;
  const char** entryKeys;

  bool updateNeeded;
} eeprom_t;
 
 /* > Function Declaration *************************************************************************/
 
 void Eeprom_init(eeprom_t* eeprom);
 void Eeprom_update(void);
 void Eeprom_write(uint32_t id, uint32_t value);
 uint32_t Eeprom_read(uint32_t id);
 
 uint32_t Eeprom_increment(uint32_t id, uint32_t maxValue);
 uint32_t Eeprom_decrement(uint32_t id, uint32_t minValue);
 
 void Eeprom_format(const uint32_t* defaults);
 void Eeprom_writeDefaults(const uint32_t* defaults);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif // _EEPROM_H