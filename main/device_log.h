/***************************************************************************************************
 * @file    : device_log.h
 * @brief   : The device log: one entry for each start (why it started, the battery, a new firmware),
 *            the last 64. The sync page reads it (docs/DATA.md section 3).
 **************************************************************************************************/

#ifndef _DEVICE_LOG_H
#define _DEVICE_LOG_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define DEVICE_LOG_MAX          64
#define DEVICE_LOG_ENTRY_SIZE   4
#define DEVICE_LOG_DELAY_MS     5000    // The entry is written 5 s after the start (with a battery reading)
#define DEVICE_LOG_NO_BATTERY   255

// Byte 0 of an entry. Values are stable: the web page shows them.
typedef enum device_log_reason_e {
  DEVICE_LOG_REASON__UNKNOWN = 0,
  DEVICE_LOG_REASON__POWER_ON,
  DEVICE_LOG_REASON__RESTART,
  DEVICE_LOG_REASON__CRASH,
  DEVICE_LOG_REASON__WATCHDOG,
  DEVICE_LOG_REASON__BROWN_OUT,
  DEVICE_LOG_REASON__WAKE,
  DEVICE_LOG_REASON__OTHER,
} device_log_reason_t;

#define DEVICE_LOG_FLAG__NEW_FIRMWARE   0x01

/* > Function Declarations ************************************************************************/

// App_init: reads the log. The first call after a start also notes the reason of this start.
// A later call (after a factory reset erased the flash) writes the log in RAM back.
void    DeviceLog_init(void);
// Each loop: writes the entry of this start DEVICE_LOG_DELAY_MS after the start (one time)
void    DeviceLog_update(uint32_t now);

uint8_t DeviceLog_count(void);
// Entry `index` (0 = newest) into `out` (4 bytes). false = no entry
bool    DeviceLog_get(uint8_t index, uint8_t* out);

#endif // _DEVICE_LOG_H
