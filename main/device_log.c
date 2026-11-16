/***************************************************************************************************
 * @file    : device_log.c
 * @brief   : The device log. See device_log.h and docs/DATA.md section 3.
 *
 * Flash ("data" namespace, key "log"): u8 version 1, u8 count, u8 next boot number, u32 CRC-32 of
 * the firmware version text of the newest entry, then count x 4 bytes (newest first). A wrong
 * blob = an empty log. One write each start, 5 s after it.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "device_log.h"

#include <string.h>

#include "battery.h"
#include "board.h"
#include "data_store.h"
#include "stapp.h"

/* > Defines **************************************************************************************/
#define LOG_VERSION   1
#define LOG_HEADER    7
#define LOG_MAX_SIZE  (LOG_HEADER + DEVICE_LOG_MAX * DEVICE_LOG_ENTRY_SIZE)

/* > Local Function Declarations ******************************************************************/
static void _read(void);
static bool _write(void);

/* > Local Variables ******************************************************************************/
static uint8_t  s_entries[DEVICE_LOG_MAX * DEVICE_LOG_ENTRY_SIZE];
static uint8_t  s_count = 0;
static uint8_t  s_nextBoot = 0;
static uint32_t s_versionCrc = 0;
static bool     s_started = false;      // DeviceLog_init() ran after this start
static bool     s_written = false;      // The entry of this start is in flash
static uint8_t  s_reason = DEVICE_LOG_REASON__UNKNOWN;
static uint8_t  s_buffer[LOG_MAX_SIZE];  // Static: the main loop has a small stack

/* > Function Definitions *************************************************************************/

void DeviceLog_init(void) {
  if(s_started) {
    // The flash was erased (factory reset): the log stays, it has no personal data
    _write();
    return;
  }
  s_started = true;
  s_reason = Board_getResetReason();
  _read();
}

void DeviceLog_update(uint32_t now) {
  if(!s_started || s_written || now < DEVICE_LOG_DELAY_MS)
    return;
  s_written = true;

  const char* version = Board_getFwVersion();
  const uint32_t crc = Stapp_crc32((const uint8_t*)version, (uint32_t)strlen(version));
  const uint8_t level = Battery_readLevel();
  const uint8_t type = Battery_getTypeUsed();

  uint8_t entry[DEVICE_LOG_ENTRY_SIZE];
  entry[0] = s_reason;
  entry[1] = (type <= BATTERY_TYPE__USB || level > 100) ? DEVICE_LOG_NO_BATTERY : level;
  entry[2] = (s_count > 0 && crc != s_versionCrc) ? DEVICE_LOG_FLAG__NEW_FIRMWARE : 0;
  entry[3] = s_nextBoot;

  // Newest first: the others move back, the oldest goes
  const uint8_t keep = (s_count < DEVICE_LOG_MAX) ? s_count : DEVICE_LOG_MAX - 1;
  memmove(&s_entries[DEVICE_LOG_ENTRY_SIZE], s_entries, (size_t)keep * DEVICE_LOG_ENTRY_SIZE);
  memcpy(s_entries, entry, DEVICE_LOG_ENTRY_SIZE);
  s_count = (uint8_t)(keep + 1);
  s_nextBoot++;
  s_versionCrc = crc;
  _write();
}

uint8_t DeviceLog_count(void) {
  return s_count;
}

bool DeviceLog_get(uint8_t index, uint8_t* out) {
  if(out == NULL || index >= s_count)
    return false;
  memcpy(out, &s_entries[index * DEVICE_LOG_ENTRY_SIZE], DEVICE_LOG_ENTRY_SIZE);
  return true;
}

/* > Local Function Definitions *******************************************************************/

static void _read(void) {
  uint8_t* buffer = s_buffer;
  s_count = 0;
  s_nextBoot = 0;
  s_versionCrc = 0;

  size_t length = 0;
  if(!DataStore_readAny("log", buffer, sizeof(s_buffer), &length) || length < LOG_HEADER)
    return;
  const uint8_t count = buffer[1];
  if(buffer[0] != LOG_VERSION || count > DEVICE_LOG_MAX || length != LOG_HEADER + (size_t)count * DEVICE_LOG_ENTRY_SIZE)
    return;
  {
    s_count = count;
    s_nextBoot = buffer[2];
    s_versionCrc = (uint32_t)buffer[3] | ((uint32_t)buffer[4] << 8) | ((uint32_t)buffer[5] << 16) | ((uint32_t)buffer[6] << 24);
    memcpy(s_entries, &buffer[LOG_HEADER], (size_t)count * DEVICE_LOG_ENTRY_SIZE);
    for(uint8_t i = 0; i < count; i++) {
      if(s_entries[i * DEVICE_LOG_ENTRY_SIZE] > DEVICE_LOG_REASON__OTHER)
        s_entries[i * DEVICE_LOG_ENTRY_SIZE] = DEVICE_LOG_REASON__UNKNOWN;
    }
    return;
  }
}

static bool _write(void) {
  uint8_t* buffer = s_buffer;
  buffer[0] = LOG_VERSION;
  buffer[1] = s_count;
  buffer[2] = s_nextBoot;
  buffer[3] = (uint8_t)(s_versionCrc & 0xFF);
  buffer[4] = (uint8_t)((s_versionCrc >> 8) & 0xFF);
  buffer[5] = (uint8_t)((s_versionCrc >> 16) & 0xFF);
  buffer[6] = (uint8_t)(s_versionCrc >> 24);
  memcpy(&buffer[LOG_HEADER], s_entries, (size_t)s_count * DEVICE_LOG_ENTRY_SIZE);
  return DataStore_write("log", buffer, LOG_HEADER + (size_t)s_count * DEVICE_LOG_ENTRY_SIZE);
}
