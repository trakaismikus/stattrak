/***************************************************************************************************
 * @file    : data_store.h
 * @brief   : The flash namespace "data": app data, the device log and the start picture
 *            (docs/DATA.md section 1). One NVS handle for the three modules.
 **************************************************************************************************/

#ifndef _DATA_STORE_H
#define _DATA_STORE_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* > Function Declarations ************************************************************************/

// Opens the namespace (App_init). DataStore_deinit() closes it before a factory reset.
void     DataStore_init(void);
void     DataStore_deinit(void);
bool     DataStore_isOpen(void);

// A blob: the size in `*length` (in: the buffer size). false = missing or an other size.
bool     DataStore_read(const char* key, void* data, size_t* length);
// A blob of any size up to `max` bytes; its size into `*length`
bool     DataStore_readAny(const char* key, void* data, size_t max, size_t* length);
// Writes and commits. false = the flash write failed (full or a fault).
bool     DataStore_write(const char* key, const void* data, size_t length);
void     DataStore_erase(const char* key);     // A missing key is no fault

// NVS entries of 32 bytes that are free for new data (without the page that the NVS keeps free)
uint32_t DataStore_availableEntries(void);

#endif // _DATA_STORE_H
