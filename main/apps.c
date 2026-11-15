/***************************************************************************************************
 * @file    : apps.c
 * @brief   : The app list and the storage of installed apps. See apps.h and docs/APPS.md.
 *
 * Everything that comes from flash is checked: the list (version, count, ids), each package
 * (Stapp_check, the same check as for a new package), the setting values and the saved values.
 * A broken part is left out; the built-in apps are always in the list.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "apps.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

/* > Defines **************************************************************************************/
#define LIST_VERSION      1
#define LIST_HEADER       2                       // version, count
#define LIST_ENTRY_SIZE   5                       // u32 id, u8 flags
#define LIST_MAX_SIZE     (LIST_HEADER + APPS_LIST_MAX * LIST_ENTRY_SIZE)
#define KEY_SIZE          12                      // "p" + 8 hex digits + 0

/* > Type definitions *****************************************************************************/
typedef struct list_entry_s {
  uint32_t id;
  uint8_t  flags;     // APPS_FLAG__HIDDEN only
} list_entry_t;

// What the firmware keeps in RAM of an installed app (the package itself stays in flash)
typedef struct installed_s {
  uint32_t id;
  uint16_t size;
  uint16_t version;
  uint8_t  settingCount;
  uint8_t  valueCount[STAPP_MAX_SETTINGS];
  uint8_t  defaults[STAPP_MAX_SETTINGS];
  uint8_t  settings[STAPP_MAX_SETTINGS];
  uint8_t  errors;
  char     name[STAPP_MAX_NAME + 1];
  uint8_t  picture[STAPP_PICTURE_SIZE];
} installed_t;

/* > Local Function Declarations ******************************************************************/
static void _key(char* key, char prefix, uint32_t id);
static void _defaultList(void);
static bool _addEntry(uint32_t id, uint8_t flags);
static int8_t _findEntry(uint32_t id);
static installed_t* _findInstalled(uint32_t id);
static bool _cacheInstalled(uint32_t id, const uint8_t* package, const stapp_info_t* info, bool readFlash);
static uint32_t _usedBytes(void);
static uint8_t _shownCount(void);
static bool _writeList(void);
static bool _writeBlob(char prefix, uint32_t id, const void* data, uint32_t size);
static void _eraseKey(char prefix, uint32_t id);
static bool _commit(void);

/* > Local Variables ******************************************************************************/
static const char* TAG = "Apps";

static const struct {
  uint32_t  id;
  view_id_t view;
  const char* name;
} kBuiltin[APPS_BUILTIN_COUNT] = {
  { APPS_ID__KILLS,     VIEW_ID__TOTAL,     "KILLS" },
  { APPS_ID__KILL_DEAD, VIEW_ID__KILL_DEAD, "KILLS + DEAD" },
  { APPS_ID__STATS,     VIEW_ID__STATS,     "STATS" },
  { APPS_ID__TITAN,     VIEW_ID__TITAN,     "TITAN" },
};

static nvs_handle_t s_handle;
static bool         s_open = false;

static list_entry_t s_list[APPS_LIST_MAX];
static uint8_t      s_listCount = 0;

static installed_t  s_installed[APPS_MAX_INSTALLED];
static uint8_t      s_installedCount = 0;

// The start check, the package from Bluetooth and the app settings view use this buffer
static uint8_t      s_work[STAPP_MAX_SIZE];
static uint8_t      s_listBuf[LIST_MAX_SIZE];

/* > Function Definitions *************************************************************************/

void Apps_init(void) {
  Apps_deinit();
  s_listCount = 0;
  s_installedCount = 0;

  if(nvs_open("apps", NVS_READWRITE, &s_handle) == ESP_OK) {
    s_open = true;
  } else {
    ESP_LOGE(TAG, "Cannot open the app storage");
  }

  size_t size = 0;
  bool good = s_open && nvs_get_blob(s_handle, "list", NULL, &size) == ESP_OK &&
              size >= LIST_HEADER && size <= sizeof(s_listBuf);
  if(good) {
    size_t length = sizeof(s_listBuf);
    good = nvs_get_blob(s_handle, "list", s_listBuf, &length) == ESP_OK && length == size &&
           s_listBuf[0] == LIST_VERSION && s_listBuf[1] <= APPS_LIST_MAX &&
           size == LIST_HEADER + (size_t)s_listBuf[1] * LIST_ENTRY_SIZE;
  }

  if(!good) {
    _defaultList();
    return;
  }

  for(uint8_t i = 0; i < s_listBuf[1]; i++) {
    const uint8_t* p = &s_listBuf[LIST_HEADER + i * LIST_ENTRY_SIZE];
    const uint32_t id = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    const uint8_t flags = p[4] & APPS_FLAG__HIDDEN;

    if(_findEntry(id) >= 0)
      continue;     // The same id twice
    if(Apps_isBuiltin(id)) {
      _addEntry(id, flags);
    } else if(s_installedCount < APPS_MAX_INSTALLED && _cacheInstalled(id, NULL, NULL, true)) {
      _addEntry(id, flags);
    }
  }

  // The built-in apps are always in the list
  for(uint8_t b = 0; b < APPS_BUILTIN_COUNT; b++) {
    if(_findEntry(kBuiltin[b].id) < 0)
      _addEntry(kBuiltin[b].id, 0);
  }

  // An app with too many errors stays hidden
  for(uint8_t i = 0; i < s_installedCount; i++) {
    if(s_installed[i].errors >= APPS_ERROR_LIMIT) {
      const int8_t e = _findEntry(s_installed[i].id);
      if(e >= 0)
        s_list[e].flags |= APPS_FLAG__HIDDEN;
    }
  }
}

void Apps_deinit(void) {
  if(s_open) {
    nvs_close(s_handle);
    s_open = false;
  }
}

uint8_t Apps_getMenu(apps_item_t* items, uint8_t max) {
  uint8_t count = 0;

  if(items == NULL || max == 0)
    return 0;

  for(uint8_t i = 0; i < s_listCount && count < max; i++) {
    if(s_list[i].flags & APPS_FLAG__HIDDEN)
      continue;
    items[count].id = s_list[i].id;
    items[count].view = VIEW_ID__APP_HOST;
    for(uint8_t b = 0; b < APPS_BUILTIN_COUNT; b++) {
      if(kBuiltin[b].id == s_list[i].id)
        items[count].view = kBuiltin[b].view;
    }
    count++;
  }

  // At least one app before SETTINGS
  if(count == 0) {
    items[0].id = APPS_ID__KILLS;
    items[0].view = VIEW_ID__TOTAL;
    count = 1;
  }
  return count;
}

uint8_t Apps_getCount(void) {
  return s_listCount;
}

bool Apps_getEntry(uint8_t index, uint32_t* id, uint8_t* flags) {
  if(index >= s_listCount)
    return false;
  if(id)
    *id = s_list[index].id;
  if(flags)
    *flags = Apps_getFlags(s_list[index].id);
  return true;
}

uint8_t Apps_getInstalledCount(void) {
  return s_installedCount;
}

uint32_t Apps_getFreeBytes(void) {
  const uint32_t used = _usedBytes();
  return (used < APPS_MAX_BYTES) ? (APPS_MAX_BYTES - used) : 0;
}

bool Apps_isBuiltin(uint32_t id) {
  for(uint8_t b = 0; b < APPS_BUILTIN_COUNT; b++) {
    if(kBuiltin[b].id == id)
      return true;
  }
  return false;
}

bool Apps_isInstalled(uint32_t id) {
  return _findInstalled(id) != NULL;
}

uint8_t Apps_getFlags(uint32_t id) {
  const int8_t e = _findEntry(id);
  if(e < 0)
    return 0;

  uint8_t flags = s_list[e].flags & APPS_FLAG__HIDDEN;
  if(Apps_isBuiltin(id))
    flags |= APPS_FLAG__BUILTIN;

  const installed_t* app = _findInstalled(id);
  if(app != NULL) {
    if(app->settingCount > 0)
      flags |= APPS_FLAG__SETTINGS;
    if(app->errors >= APPS_ERROR_LIMIT)
      flags |= APPS_FLAG__ERROR;
  }
  return flags;
}

const char* Apps_getName(uint32_t id) {
  for(uint8_t b = 0; b < APPS_BUILTIN_COUNT; b++) {
    if(kBuiltin[b].id == id)
      return kBuiltin[b].name;
  }
  const installed_t* app = _findInstalled(id);
  return app ? app->name : NULL;
}

uint16_t Apps_getVersion(uint32_t id) {
  const installed_t* app = _findInstalled(id);
  return app ? app->version : 0;
}

uint16_t Apps_getSize(uint32_t id) {
  const installed_t* app = _findInstalled(id);
  return app ? app->size : 0;
}

const uint8_t* Apps_getPicture(uint32_t id) {
  const installed_t* app = _findInstalled(id);
  return app ? app->picture : NULL;
}

uint8_t Apps_getSettingCount(uint32_t id) {
  const installed_t* app = _findInstalled(id);
  return app ? app->settingCount : 0;
}

uint8_t Apps_getSettingValueCount(uint32_t id, uint8_t setting) {
  const installed_t* app = _findInstalled(id);
  return (app && setting < app->settingCount) ? app->valueCount[setting] : 0;
}

uint8_t Apps_getSetting(uint32_t id, uint8_t setting) {
  const installed_t* app = _findInstalled(id);
  return (app && setting < app->settingCount) ? app->settings[setting] : 0;
}

apps_result_t Apps_setSetting(uint32_t id, uint8_t setting, uint8_t value) {
  installed_t* app = _findInstalled(id);
  if(app == NULL)
    return APPS_ERR_UNKNOWN;
  if(setting >= app->settingCount || value >= app->valueCount[setting])
    return APPS_ERR_BAD;
  if(app->settings[setting] == value)
    return APPS_OK;   // No change, no flash write

  app->settings[setting] = value;
  return _writeBlob('s', id, app->settings, app->settingCount) ? APPS_OK : APPS_ERR_FLASH;
}

stapp_result_t Apps_loadPackage(uint32_t id, uint8_t* buffer, stapp_info_t* info) {
  char key[KEY_SIZE];
  size_t size = 0;

  if(!s_open || buffer == NULL || info == NULL || !Apps_isInstalled(id))
    return STAPP_ERR_SIZE;

  _key(key, 'p', id);
  if(nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK || size > STAPP_MAX_SIZE)
    return STAPP_ERR_SIZE;

  size_t length = STAPP_MAX_SIZE;
  if(nvs_get_blob(s_handle, key, buffer, &length) != ESP_OK || length != size)
    return STAPP_ERR_SIZE;

  const stapp_result_t result = Stapp_check(buffer, (uint32_t)size, info);
  if(result != STAPP_OK)
    return result;
  return (info->id == id) ? STAPP_OK : STAPP_ERR_HEADER;
}

void Apps_loadValues(uint32_t id, int32_t* values, uint8_t count) {
  char key[KEY_SIZE];
  size_t size = 0;

  if(values == NULL || count == 0)
    return;
  if(count > STAPP_MAX_SAVED)
    count = STAPP_MAX_SAVED;
  memset(values, 0, count * sizeof(int32_t));

  if(!s_open)
    return;
  _key(key, 'v', id);
  if(nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK || size != count * sizeof(int32_t))
    return;

  size_t length = size;
  if(nvs_get_blob(s_handle, key, values, &length) != ESP_OK || length != size)
    memset(values, 0, count * sizeof(int32_t));
}

apps_result_t Apps_saveValues(uint32_t id, const int32_t* values, uint8_t count) {
  if(!Apps_isInstalled(id))
    return APPS_ERR_UNKNOWN;
  if(values == NULL || count == 0 || count > STAPP_MAX_SAVED)
    return APPS_ERR_BAD;
  return _writeBlob('v', id, values, count * sizeof(int32_t)) ? APPS_OK : APPS_ERR_FLASH;
}

apps_result_t Apps_canInstall(uint32_t id, uint32_t size) {
  // Ids 0 .. 255 are for the built-in apps (also later ones)
  if(id <= STAPP_RESERVED_IDS || id == 0xFFFFFFFFu)
    return APPS_ERR_BAD;
  if(size > STAPP_MAX_SIZE)
    return APPS_ERR_TOO_LARGE;

  // An installed app with the same id is replaced: its space is free again
  const installed_t* old = _findInstalled(id);
  if(old == NULL && s_installedCount >= APPS_MAX_INSTALLED)
    return APPS_ERR_TOO_MANY;

  const uint32_t used = _usedBytes() - (old ? old->size : 0);
  if(used + size > APPS_MAX_BYTES)
    return APPS_ERR_NO_SPACE;
  return APPS_OK;
}

apps_result_t Apps_install(const uint8_t* package, const stapp_info_t* info) {
  if(package == NULL || info == NULL)
    return APPS_ERR_BAD;

  const apps_result_t can = Apps_canInstall(info->id, info->size);
  if(can != APPS_OK)
    return can;
  if(!s_open)
    return APPS_ERR_FLASH;

  // One flash write for the package. The list changes only after it is in flash.
  if(!_writeBlob('p', info->id, package, info->size))
    return APPS_ERR_FLASH;

  const installed_t* old = _findInstalled(info->id);
  const bool isNew = (old == NULL);
  const bool wasSwitchedOff = (old != NULL && old->errors >= APPS_ERROR_LIMIT);

  // A new app starts clean: values of the same id can be left from a delete that a power loss stopped
  if(isNew) {
    _eraseKey('v', info->id);
    _eraseKey('s', info->id);
    _eraseKey('e', info->id);
  }
  _cacheInstalled(info->id, package, info, false);

  // A new version starts without errors
  installed_t* app = _findInstalled(info->id);
  if(app != NULL && app->errors > 0) {
    app->errors = 0;
    _eraseKey('e', info->id);
  }

  if(isNew) {
    _addEntry(info->id, 0);
    if(!_writeList())
      return APPS_ERR_FLASH;
  } else {
    // A new version of an app that was switched off for errors: shown again
    const int8_t e = _findEntry(info->id);
    if(e >= 0 && wasSwitchedOff && (s_list[e].flags & APPS_FLAG__HIDDEN)) {
      s_list[e].flags &= (uint8_t)~APPS_FLAG__HIDDEN;
      if(!_writeList())
        return APPS_ERR_FLASH;
    }
  }
  _commit();
  return APPS_OK;
}

apps_result_t Apps_delete(uint32_t id) {
  if(Apps_isBuiltin(id))
    return APPS_ERR_BUILTIN;

  installed_t* app = _findInstalled(id);
  const int8_t e = _findEntry(id);
  if(app == NULL || e < 0)
    return APPS_ERR_UNKNOWN;

  // The list first: an app without a list entry is gone, even if the power goes off now
  memmove(&s_list[e], &s_list[e + 1], (size_t)(s_listCount - e - 1) * sizeof(s_list[0]));
  s_listCount--;
  const bool listWritten = _writeList();

  const uint8_t idx = (uint8_t)(app - s_installed);
  memmove(&s_installed[idx], &s_installed[idx + 1], (size_t)(s_installedCount - idx - 1) * sizeof(s_installed[0]));
  s_installedCount--;

  _eraseKey('p', id);
  _eraseKey('v', id);
  _eraseKey('s', id);
  _eraseKey('e', id);
  _commit();
  return listWritten ? APPS_OK : APPS_ERR_FLASH;
}

apps_result_t Apps_setHidden(uint32_t id, bool hidden) {
  const int8_t e = _findEntry(id);
  if(e < 0)
    return APPS_ERR_UNKNOWN;

  const bool isHidden = (s_list[e].flags & APPS_FLAG__HIDDEN) != 0;
  installed_t* app = _findInstalled(id);

  if(hidden) {
    if(isHidden)
      return APPS_OK;
    if(_shownCount() <= 1)
      return APPS_ERR_LAST_SHOWN;
    s_list[e].flags |= APPS_FLAG__HIDDEN;
  } else {
    // Shown again: an app that was switched off for errors gets a new chance
    if(app != NULL && app->errors > 0) {
      app->errors = 0;
      _eraseKey('e', id);
    }
    if(!isHidden) {
      _commit();
      return APPS_OK;
    }
    s_list[e].flags &= (uint8_t)~APPS_FLAG__HIDDEN;
  }
  return _writeList() ? APPS_OK : APPS_ERR_FLASH;
}

apps_result_t Apps_setOrder(const uint32_t* ids, uint8_t count) {
  list_entry_t order[APPS_LIST_MAX];

  if(ids == NULL || count != s_listCount)
    return APPS_ERR_BAD;

  for(uint8_t i = 0; i < count; i++) {
    const int8_t e = _findEntry(ids[i]);
    if(e < 0)
      return APPS_ERR_BAD;
    for(uint8_t j = 0; j < i; j++) {
      if(ids[j] == ids[i])
        return APPS_ERR_BAD;   // The same id twice
    }
    order[i] = s_list[e];
  }

  bool changed = false;
  for(uint8_t i = 0; i < count; i++)
    changed = changed || (order[i].id != s_list[i].id);
  if(!changed)
    return APPS_OK;   // No change, no flash write

  memcpy(s_list, order, count * sizeof(order[0]));
  return _writeList() ? APPS_OK : APPS_ERR_FLASH;
}

void Apps_noteError(uint32_t id) {
  installed_t* app = _findInstalled(id);
  if(app == NULL)
    return;

  if(app->errors < 0xFF)
    app->errors++;
  ESP_LOGW(TAG, "App %08lx: error %u", (unsigned long)id, (unsigned)app->errors);
  _writeBlob('e', id, &app->errors, 1);

  if(app->errors >= APPS_ERROR_LIMIT) {
    const int8_t e = _findEntry(id);
    if(e >= 0 && !(s_list[e].flags & APPS_FLAG__HIDDEN)) {
      // Hidden even if it was the last shown app: the menu then shows KILLS
      s_list[e].flags |= APPS_FLAG__HIDDEN;
      _writeList();
    }
  }
}

uint8_t* Apps_getWorkBuffer(void) {
  return s_work;
}

/* > Local Function Definitions *******************************************************************/

static void _key(char* key, char prefix, uint32_t id) {
  snprintf(key, KEY_SIZE, "%c%08lx", prefix, (unsigned long)id);
}

static void _defaultList(void) {
  s_listCount = 0;
  for(uint8_t b = 0; b < APPS_BUILTIN_COUNT; b++)
    _addEntry(kBuiltin[b].id, 0);
}

static bool _addEntry(uint32_t id, uint8_t flags) {
  if(s_listCount >= APPS_LIST_MAX)
    return false;
  s_list[s_listCount].id = id;
  s_list[s_listCount].flags = flags & APPS_FLAG__HIDDEN;
  s_listCount++;
  return true;
}

static int8_t _findEntry(uint32_t id) {
  for(uint8_t i = 0; i < s_listCount; i++) {
    if(s_list[i].id == id)
      return (int8_t)i;
  }
  return -1;
}

static installed_t* _findInstalled(uint32_t id) {
  for(uint8_t i = 0; i < s_installedCount; i++) {
    if(s_installed[i].id == id)
      return &s_installed[i];
  }
  return NULL;
}

// Puts the RAM part of an installed app in the cache. With readFlash, the package comes from
// flash into the work buffer and is checked; else `package` and `info` are a checked package.
static bool _cacheInstalled(uint32_t id, const uint8_t* package, const stapp_info_t* info, bool readFlash) {
  stapp_info_t readInfo;
  char key[KEY_SIZE];

  if(readFlash) {
    size_t size = 0;
    _key(key, 'p', id);
    if(!s_open || nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK || size > STAPP_MAX_SIZE)
      return false;
    size_t length = STAPP_MAX_SIZE;
    if(nvs_get_blob(s_handle, key, s_work, &length) != ESP_OK || length != size)
      return false;
    if(Stapp_check(s_work, (uint32_t)size, &readInfo) != STAPP_OK || readInfo.id != id)
      return false;
    if(_usedBytes() + size > APPS_MAX_BYTES)
      return false;   // More than the 24 KB (flash from somewhere else): left out
    package = s_work;
    info = &readInfo;
  }

  installed_t* app = _findInstalled(id);
  if(app == NULL) {
    if(s_installedCount >= APPS_MAX_INSTALLED)
      return false;
    app = &s_installed[s_installedCount++];
    memset(app, 0, sizeof(*app));
    app->id = id;
    if(readFlash) {
      // The error count of an app that was installed before
      uint8_t errors = 0;
      size_t length = 1;
      _key(key, 'e', id);
      if(nvs_get_blob(s_handle, key, &errors, &length) == ESP_OK && length == 1)
        app->errors = errors;
    }
  }

  app->size = info->size;
  app->version = info->version;
  memcpy(app->name, info->name, sizeof(app->name));
  app->name[STAPP_MAX_NAME] = 0;
  memcpy(app->picture, Stapp_picture(package), STAPP_PICTURE_SIZE);

  // Settings: the stored values if they fit this package, else the defaults
  uint8_t stored[STAPP_MAX_SETTINGS] = { 0 };
  size_t storedLength = 0;
  _key(key, 's', id);
  if(s_open && nvs_get_blob(s_handle, key, NULL, &storedLength) == ESP_OK && storedLength == info->settingCount) {
    size_t length = sizeof(stored);
    if(nvs_get_blob(s_handle, key, stored, &length) != ESP_OK || length != storedLength)
      storedLength = 0;
  } else {
    storedLength = 0;
  }

  app->settingCount = info->settingCount;
  for(uint8_t s = 0; s < STAPP_MAX_SETTINGS; s++) {
    const char* name;
    uint8_t count = 0, def = 0;
    if(s < info->settingCount && Stapp_setting(package, info, s, &name, &count, &def)) {
      app->valueCount[s] = count;
      app->defaults[s] = def;
      app->settings[s] = (storedLength > 0 && stored[s] < count) ? stored[s] : def;
    } else {
      app->valueCount[s] = 0;
      app->defaults[s] = 0;
      app->settings[s] = 0;
    }
  }
  return true;
}

static uint32_t _usedBytes(void) {
  uint32_t total = 0;
  for(uint8_t i = 0; i < s_installedCount; i++)
    total += s_installed[i].size;
  return total;
}

static uint8_t _shownCount(void) {
  uint8_t count = 0;
  for(uint8_t i = 0; i < s_listCount; i++) {
    if(!(s_list[i].flags & APPS_FLAG__HIDDEN))
      count++;
  }
  return count;
}

static bool _writeList(void) {
  s_listBuf[0] = LIST_VERSION;
  s_listBuf[1] = s_listCount;
  for(uint8_t i = 0; i < s_listCount; i++) {
    uint8_t* p = &s_listBuf[LIST_HEADER + i * LIST_ENTRY_SIZE];
    p[0] = (uint8_t)(s_list[i].id & 0xFF);
    p[1] = (uint8_t)((s_list[i].id >> 8) & 0xFF);
    p[2] = (uint8_t)((s_list[i].id >> 16) & 0xFF);
    p[3] = (uint8_t)(s_list[i].id >> 24);
    p[4] = s_list[i].flags & APPS_FLAG__HIDDEN;
  }

  if(!s_open || nvs_set_blob(s_handle, "list", s_listBuf, LIST_HEADER + (size_t)s_listCount * LIST_ENTRY_SIZE) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot write the app list");
    return false;
  }
  return _commit();
}

static bool _writeBlob(char prefix, uint32_t id, const void* data, uint32_t size) {
  char key[KEY_SIZE];
  _key(key, prefix, id);
  if(!s_open || nvs_set_blob(s_handle, key, data, size) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot write %s", key);
    return false;
  }
  return _commit();
}

/* > The name part of the app sync Bluetooth name, "StatTrak | <part>" (key "bname", 1 .. 12 bytes) ****/

bool Apps_isDeviceNameValid(const char* name) {
  if(name == NULL)
    return false;
  const size_t length = strlen(name);
  if(length < 1 || length > APPS_DEVICE_NAME_MAX || name[0] == ' ' || name[length - 1] == ' ')
    return false;
  for(size_t i = 0; i < length; i++) {
    if((uint8_t)name[i] < 0x20 || (uint8_t)name[i] > 0x7E)
      return false;
  }
  return true;
}

void Apps_getDeviceName(char* out) {
  char name[APPS_DEVICE_NAME_MAX + 1] = { 0 };
  size_t length = APPS_DEVICE_NAME_MAX;

  out[0] = '\0';
  if(!s_open || nvs_get_blob(s_handle, "bname", name, &length) != ESP_OK || length > APPS_DEVICE_NAME_MAX)
    return;
  name[length] = '\0';
  if(Apps_isDeviceNameValid(name))
    strcpy(out, name);
}

apps_result_t Apps_setDeviceName(const char* name) {
  if(name == NULL)
    return APPS_ERR_BAD;
  if(name[0] == '\0') {
    // Back to "StatTrak"
    if(s_open)
      nvs_erase_key(s_handle, "bname");   // ESP_ERR_NVS_NOT_FOUND is good too
    return _commit() ? APPS_OK : APPS_ERR_FLASH;
  }
  if(!Apps_isDeviceNameValid(name))
    return APPS_ERR_BAD;
  if(!s_open || nvs_set_blob(s_handle, "bname", name, strlen(name)) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot write the device name");
    return APPS_ERR_FLASH;
  }
  return _commit() ? APPS_OK : APPS_ERR_FLASH;
}

static void _eraseKey(char prefix, uint32_t id) {
  char key[KEY_SIZE];
  _key(key, prefix, id);
  if(s_open)
    nvs_erase_key(s_handle, key);   // ESP_ERR_NVS_NOT_FOUND is good too
}

static bool _commit(void) {
  return s_open && nvs_commit(s_handle) == ESP_OK;
}
