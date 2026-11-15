/***************************************************************************************************
 * @file    : backup.c
 * @brief   : Restore of a backup. See backup.h and docs/DATA.md section 5.4.
 *
 * "STBK", u8 version 1, then sections: u8 type, u16 length, data. Every value from the page is
 * checked like a value from flash: a key that this firmware does not have, a value above its
 * largest valid value, an app that is not installed or has other counts = skipped.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "backup.h"

#include <string.h>

#include "app.h"
#include "apps.h"
#include "eeprom.h"
#include "start_pic.h"
#include "stapp.h"

/* > Defines **************************************************************************************/
#define BACKUP_VERSION      1
#define SECTION_HEADER      3

enum section_type_e {
  SECTION__VALUE = 1,
  SECTION__APP_ORDER = 2,
  SECTION__APP_SETTINGS = 3,
  SECTION__APP_VALUES = 4,
  SECTION__DEVICE_NAME = 5,
  SECTION__START_PIC = 6,
};

/* > Local Function Declarations ******************************************************************/
static void _value(const uint8_t* d, uint16_t length);
static void _appOrder(const uint8_t* d, uint16_t length);
static void _appSettings(const uint8_t* d, uint16_t length);
static void _appValues(const uint8_t* d, uint16_t length);
static void _deviceName(const uint8_t* d, uint16_t length);
static void _startPic(const uint8_t* d, uint16_t length);
static void _count(uint8_t* counter, uint32_t n);
static uint32_t _get32(const uint8_t* in);

/* > Local Variables ******************************************************************************/
static uint8_t  s_written = 0;
static uint8_t  s_skipped = 0;
static bool     s_settingsChanged = false;
// Work memory (static: the main loop has a small stack)
static uint32_t s_ids[APPS_LIST_MAX];
static uint8_t  s_hidden[APPS_LIST_MAX];
static int32_t  s_values[STAPP_MAX_SAVED];

/* > Function Definitions *************************************************************************/

backup_result_t Backup_restore(const uint8_t* data, uint32_t size, uint8_t* written, uint8_t* skipped) {
  s_written = 0;
  s_skipped = 0;
  s_settingsChanged = false;

  if(data == NULL || size < BACKUP_MIN_SIZE || memcmp(data, "STBK", 4) != 0)
    return BACKUP_ERR_MAGIC;
  if(data[4] != BACKUP_VERSION)
    return BACKUP_ERR_VERSION;

  // 1. Walk: each section inside the data
  uint32_t pos = BACKUP_MIN_SIZE;
  while(pos < size) {
    if(pos + SECTION_HEADER > size)
      return BACKUP_ERR_LAYOUT;
    const uint16_t length = (uint16_t)(data[pos + 1] | (data[pos + 2] << 8));
    if(pos + SECTION_HEADER + length > size)
      return BACKUP_ERR_LAYOUT;
    pos += SECTION_HEADER + length;
  }

  // 2. Write
  pos = BACKUP_MIN_SIZE;
  while(pos < size) {
    const uint8_t type = data[pos];
    const uint16_t length = (uint16_t)(data[pos + 1] | (data[pos + 2] << 8));
    const uint8_t* d = &data[pos + SECTION_HEADER];
    switch(type) {
      case SECTION__VALUE:        _value(d, length); break;
      case SECTION__APP_ORDER:    _appOrder(d, length); break;
      case SECTION__APP_SETTINGS: _appSettings(d, length); break;
      case SECTION__APP_VALUES:   _appValues(d, length); break;
      case SECTION__DEVICE_NAME:  _deviceName(d, length); break;
      case SECTION__START_PIC:    _startPic(d, length); break;
      default:                    _count(&s_skipped, 1); break;   // A part of a newer firmware
    }
    pos += SECTION_HEADER + length;
  }

  // The new settings into use at once (brightness, battery type, ...)
  if(s_settingsChanged)
    App_reloadSettings();

  if(written)
    *written = s_written;
  if(skipped)
    *skipped = s_skipped;
  return BACKUP_OK;
}

/* > Local Function Definitions *******************************************************************/

// u8 key length (1..15), key, u32 value
static void _value(const uint8_t* d, uint16_t length) {
  if(length < 1 || d[0] < 1 || d[0] > 15 || length != 1u + d[0] + 4u) {
    _count(&s_skipped, 1);
    return;
  }
  const uint8_t keyLength = d[0];
  const uint32_t value = _get32(&d[1 + keyLength]);
  for(uint8_t i = 0; i < App_getEntryCount(); i++) {
    const char* key = App_getEntryKey(i);
    if(key == NULL || strlen(key) != keyLength || memcmp(key, &d[1], keyLength) != 0)
      continue;
    // "init" marks the first start: never from a backup
    if(i == ENTRY_ID__INIT || value > App_getEntryMax(i)) {
      _count(&s_skipped, 1);
      return;
    }
    Eeprom_write(i, value);
    s_settingsChanged = true;
    _count(&s_written, 1);
    return;
  }
  _count(&s_skipped, 1);   // A key that this firmware does not have
}

// u8 count, count x (u32 id, u8 hidden): all apps of the device, each one time
static void _appOrder(const uint8_t* d, uint16_t length) {
  const uint8_t count = (length >= 1) ? d[0] : 0;
  if(length < 1 || length != 1u + 5u * count || count != Apps_getCount() || count > APPS_LIST_MAX) {
    _count(&s_skipped, (length >= 1) ? count : 1);
    return;
  }
  for(uint8_t i = 0; i < count; i++) {
    s_ids[i] = _get32(&d[1 + 5 * i]);
    s_hidden[i] = d[1 + 5 * i + 4];
  }
  if(Apps_setOrder(s_ids, count) != APPS_OK) {
    _count(&s_skipped, count);
    return;
  }

  // Shown apps first, then the hidden ones: the last shown app is never hidden
  for(uint8_t pass = 0; pass < 2; pass++) {
    for(uint8_t i = 0; i < count; i++) {
      const bool hide = (s_hidden[i] != 0);
      if(hide != (pass == 1))
        continue;
      const bool hiddenNow = (Apps_getFlags(s_ids[i]) & APPS_FLAG__HIDDEN) != 0;
      if(hiddenNow == hide || Apps_setHidden(s_ids[i], hide) == APPS_OK)
        _count(&s_written, 1);
      else
        _count(&s_skipped, 1);
    }
  }
}

// u32 id, u8 count, count x u8
static void _appSettings(const uint8_t* d, uint16_t length) {
  const uint8_t count = (length >= 5) ? d[4] : 0;
  if(length < 5 || length != 5u + count) {
    _count(&s_skipped, 1);
    return;
  }
  const uint32_t id = _get32(d);
  if(!Apps_isInstalled(id) || count != Apps_getSettingCount(id)) {
    _count(&s_skipped, count);
    return;
  }
  for(uint8_t s = 0; s < count; s++) {
    const uint8_t value = d[5 + s];
    if(value < Apps_getSettingValueCount(id, s) &&
       (Apps_getSetting(id, s) == value || Apps_setSetting(id, s, value) == APPS_OK))
      _count(&s_written, 1);
    else
      _count(&s_skipped, 1);
  }
}

// u32 id, u8 count, count x i32
static void _appValues(const uint8_t* d, uint16_t length) {
  const uint8_t count = (length >= 5) ? d[4] : 0;
  if(length < 5 || length != 5u + 4u * count || count > STAPP_MAX_SAVED) {
    _count(&s_skipped, (length >= 5) ? count : 1);
    return;
  }
  const uint32_t id = _get32(d);
  if(count == 0)
    return;
  if(!Apps_isInstalled(id) || count != Apps_getSavedCount(id)) {
    _count(&s_skipped, count);
    return;
  }
  for(uint8_t i = 0; i < count; i++)
    s_values[i] = (int32_t)_get32(&d[5 + 4 * i]);
  if(Apps_saveValues(id, s_values, count) == APPS_OK)
    _count(&s_written, count);
  else
    _count(&s_skipped, count);
}

// u8 length (0..12), the name part ("" = "StatTrak")
static void _deviceName(const uint8_t* d, uint16_t length) {
  char name[APPS_DEVICE_NAME_MAX + 1] = { 0 };
  if(length < 1 || d[0] > APPS_DEVICE_NAME_MAX || length != 1u + d[0]) {
    _count(&s_skipped, 1);
    return;
  }
  memcpy(name, &d[1], d[0]);
  if(strlen(name) != d[0] || (d[0] > 0 && !Apps_isDeviceNameValid(name)) || Apps_setDeviceName(name) != APPS_OK) {
    _count(&s_skipped, 1);
    return;
  }
  _count(&s_written, 1);
}

// u8 show (0 delete, 1 on, 2 off), then 512 bytes (not for 0)
static void _startPic(const uint8_t* d, uint16_t length) {
  if(length == 1 && d[0] == 0) {
    StartPic_delete();
    _count(&s_written, 1);
    return;
  }
  if(length != 1u + START_PIC_SIZE || d[0] < 1 || d[0] > 2 ||
     !StartPic_save(&d[1]) || !StartPic_setShown(d[0] == 1)) {
    _count(&s_skipped, 1);
    return;
  }
  _count(&s_written, 1);
}

static void _count(uint8_t* counter, uint32_t n) {
  const uint32_t sum = (uint32_t)*counter + n;
  *counter = (sum > 255) ? 255 : (uint8_t)sum;
}

static uint32_t _get32(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}
