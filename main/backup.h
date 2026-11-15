/***************************************************************************************************
 * @file    : backup.h
 * @brief   : Restore of a backup that the sync page sends (docs/DATA.md sections 5.3 and 5.4).
 *            The page reads the backup with single commands; this writes it back after the
 *            person said YES on the device.
 **************************************************************************************************/

#ifndef _BACKUP_H
#define _BACKUP_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define BACKUP_MIN_SIZE   5     // "STBK" + version

// The reason byte of END status 10. Values are stable: the web page shows them.
typedef enum backup_result_e {
  BACKUP_OK = 0,
  BACKUP_ERR_MAGIC = 1,
  BACKUP_ERR_VERSION = 2,
  BACKUP_ERR_LAYOUT = 3,      // A section is outside the data
} backup_result_t;

/* > Function Declarations ************************************************************************/

// Walks all sections first: nothing is written when the data is broken. Then writes each value
// that this device can use and counts the others as skipped (255 at most each).
backup_result_t Backup_restore(const uint8_t* data, uint32_t size, uint8_t* written, uint8_t* skipped);

#endif // _BACKUP_H
