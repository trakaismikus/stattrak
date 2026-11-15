/***************************************************************************************************
 * @file    : app_data.c
 * @brief   : The data store of each app. See app_data.h and docs/DATA.md section 2.
 *
 * Flash ("data" namespace): "m<id>" = u8 version 1, u8 columns, u16 value count; "c<id><n>" =
 * chunk n (64 values, the last one can be shorter). Chunks are written before the meta, so a
 * power loss between them keeps the old count (the data is not damaged). Everything from flash
 * is checked: a wrong meta = no data, a missing or short chunk = the data ends before it.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_data.h"

#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "data_store.h"

/* > Defines **************************************************************************************/
#define META_VERSION    1
#define META_SIZE       4
#define CHUNK_COUNT     (APP_DATA_APP_VALUES / APP_DATA_CHUNK_VALUES)   // 16
#define KEY_SIZE        16
#define NO_ID           0u
// NVS entries for a new chunk (index + part header + 256 bytes) and the meta
#define CHUNK_ENTRIES   (2 + (APP_DATA_CHUNK_VALUES * 4) / 32 + 4)

/* > Local Function Declarations ******************************************************************/
static void _load(uint32_t id);
static uint32_t _chunks(uint32_t values);
static bool _readMeta(uint32_t id, uint8_t* columns, uint16_t* count);
static void _writeChanges(void);
static uint32_t _otherValues(uint32_t id);
static uint16_t _capacity(uint32_t id);
static void _metaKey(char* key, uint32_t id);
static void _chunkKey(char* key, uint32_t id, uint8_t chunk);

/* > Local Variables ******************************************************************************/
static int32_t  s_values[APP_DATA_APP_VALUES];
static uint32_t s_id = NO_ID;          // The store in RAM
static uint8_t  s_columns = 0;         // Of the store (0 = empty store)
static uint16_t s_count = 0;           // Values
static uint16_t s_dirty = 0;           // Chunks to write (bit n = chunk n)
static bool     s_metaDirty = false;
static uint32_t s_nextFlush = 0;
static bool     s_flushNow = false;     // The app closed: write in the next AppData_update()
static bool     s_clearPending = false; // log_clear(): the flash keys of s_id go in the next write
static uint16_t s_flashCount = 0;       // Values of s_id that are in flash (for the reserve check)

// The open app (app host)
static bool     s_appOpen = false;
static uint8_t  s_appColumns = 0;
static uint32_t s_appOthers = 0;       // Values of the other apps (they do not change while the app runs)

/* > Function Definitions *************************************************************************/

void AppData_open(uint32_t id, uint8_t columns) {
  s_appOpen = false;
  if(columns == 0 || columns > APP_DATA_MAX_COLUMNS || id == NO_ID)
    return;
  _load(id);
  s_appOthers = _otherValues(id);
  s_appOpen = true;
  s_appColumns = columns;
}

void AppData_closeLater(void) {
  if(s_appOpen && (s_dirty != 0 || s_metaDirty || s_clearPending))
    s_flushNow = true;
  s_appOpen = false;
}

bool AppData_log(const int32_t* values) {
  if(!s_appOpen || values == NULL)
    return false;
  // A store of an other column count (an older version of the app) takes no rows until it is cleared
  const uint8_t columns = (s_count == 0) ? s_appColumns : s_columns;
  if(columns != s_appColumns)
    return false;
  const uint32_t left = (s_appOthers < APP_DATA_TOTAL_VALUES) ? APP_DATA_TOTAL_VALUES - s_appOthers : 0;
  if((uint32_t)s_count + columns > left || (uint32_t)s_count + columns > APP_DATA_APP_VALUES)
    return false;

  // A new chunk needs flash: the settings and the counters keep APP_DATA_RESERVE_ENTRIES. The chunks
  // that are only in RAM, or only partly in flash (they grow when they are written), need it too.
  const uint32_t chunksNow = _chunks(s_count);
  const uint32_t chunksAfter = _chunks((uint32_t)s_count + columns);
  if(chunksAfter > chunksNow) {
    const uint32_t fullInFlash = s_flashCount / APP_DATA_CHUNK_VALUES;
    const uint32_t pending = (chunksAfter > fullInFlash) ? chunksAfter - fullInFlash : 0;
    if(DataStore_availableEntries() < APP_DATA_RESERVE_ENTRIES + CHUNK_ENTRIES * pending)
      return false;
  }

  for(uint8_t c = 0; c < columns; c++) {
    s_values[s_count] = values[c];
    s_dirty |= (uint16_t)(1u << (s_count / APP_DATA_CHUNK_VALUES));
    s_count++;
  }
  s_columns = columns;
  s_metaDirty = true;
  return true;
}

uint16_t AppData_rows(void) {
  return (s_appOpen && s_columns > 0) ? (uint16_t)(s_count / s_columns) : 0;
}

int32_t AppData_get(int32_t row, int32_t column) {
  // The row first: a large row number must not overflow the index
  if(!s_appOpen || s_columns == 0 || row < 0 || row >= s_count / s_columns || column < 0 || column >= s_columns)
    return 0;
  return s_values[row * s_columns + column];
}

// log_clear() can run in an app handler that runs while a view draws (the close of an app, the
// open of the first app after the start picture): only RAM here, the flash keys go in AppData_update()
void AppData_clear(void) {
  if(!s_appOpen)
    return;
  s_count = 0;
  s_columns = 0;
  s_dirty = 0;
  s_metaDirty = false;
  s_flashCount = 0;
  s_clearPending = true;
}

void AppData_update(uint32_t now) {
  if(s_clearPending)
    s_flushNow = true;    // A clear goes to flash at once (no 30 s wait)
  if(s_dirty == 0 && !s_metaDirty && !s_clearPending) {
    s_flushNow = false;
    return;
  }
  if(!s_flushNow && (int32_t)(now - s_nextFlush) < 0)
    return;
  s_flushNow = false;
  _writeChanges();
  s_nextFlush = now + APP_DATA_FLUSH_MS;
}

void AppData_flush(void) {
  _writeChanges();
}

bool AppData_info(uint32_t id, uint8_t* columns, uint16_t* rows) {
  if(!Apps_isInstalled(id))
    return false;
  _load(id);
  if(columns)
    *columns = s_columns;
  if(rows)
    *rows = (s_columns > 0) ? (uint16_t)(s_count / s_columns) : 0;
  return true;
}

uint16_t AppData_fitRows(uint32_t id, uint8_t columns) {
  if(columns == 0 || columns > APP_DATA_MAX_COLUMNS)
    return 0;
  return (uint16_t)(_capacity(id) / columns);
}

uint8_t AppData_readValues(uint32_t id, uint16_t first, int32_t* out, uint8_t max) {
  if(out == NULL || !Apps_isInstalled(id))
    return 0;
  _load(id);
  uint8_t count = 0;
  while(count < max && (uint32_t)first + count < s_count) {
    out[count] = s_values[first + count];
    count++;
  }
  return count;
}

void AppData_erase(uint32_t id) {
  char key[KEY_SIZE];
  if(id == NO_ID)
    return;
  // The meta first: without it the chunks are not used, even if the power goes off now
  _metaKey(key, id);
  DataStore_erase(key);
  for(uint8_t n = 0; n < CHUNK_COUNT; n++) {
    _chunkKey(key, id, n);
    DataStore_erase(key);
  }
  if(s_id == id) {
    s_count = 0;
    s_columns = 0;
    s_dirty = 0;
    s_metaDirty = false;
    s_clearPending = false;
    s_flashCount = 0;
  }
}

uint32_t AppData_usedBytes(void) {
  uint32_t values = _otherValues(NO_ID);
  return values * 4u;
}

void AppData_forget(void) {
  s_id = NO_ID;
  s_count = 0;
  s_columns = 0;
  s_dirty = 0;
  s_metaDirty = false;
  s_clearPending = false;
  s_flashCount = 0;
  s_appOpen = false;
}

/* > Local Function Definitions *******************************************************************/

// The store of `id` into RAM. The store that is there now goes to flash first.
static void _load(uint32_t id) {
  char key[KEY_SIZE];
  if(id == s_id)
    return;
  _writeChanges();

  s_id = id;
  s_count = 0;
  s_columns = 0;
  s_dirty = 0;
  s_metaDirty = false;
  s_clearPending = false;
  s_flashCount = 0;

  uint8_t columns = 0;
  uint16_t count = 0;
  if(!_readMeta(id, &columns, &count))
    return;

  uint16_t loaded = 0;
  while(loaded < count) {
    const uint8_t n = (uint8_t)(loaded / APP_DATA_CHUNK_VALUES);
    uint16_t part = (uint16_t)(count - loaded);
    if(part > APP_DATA_CHUNK_VALUES)
      part = APP_DATA_CHUNK_VALUES;
    // A chunk can be longer than the meta says (a flush that a power loss stopped before the meta):
    // only the values that the meta counts are used
    size_t length = 0;
    _chunkKey(key, id, n);
    if(!DataStore_readAny(key, &s_values[loaded], APP_DATA_CHUNK_VALUES * sizeof(int32_t), &length) ||
       length < (size_t)part * sizeof(int32_t))
      break;     // Missing or short: the data ends here
    loaded = (uint16_t)(loaded + part);
  }
  // Only whole rows
  s_count = (uint16_t)(loaded - (loaded % columns));
  s_columns = (s_count > 0) ? columns : 0;
  s_flashCount = s_count;
}

static bool _readMeta(uint32_t id, uint8_t* columns, uint16_t* count) {
  char key[KEY_SIZE];
  uint8_t meta[META_SIZE];
  size_t length = META_SIZE;
  _metaKey(key, id);
  if(!DataStore_read(key, meta, &length) || meta[0] != META_VERSION)
    return false;
  const uint16_t n = (uint16_t)(meta[2] | (meta[3] << 8));
  if(meta[1] < 1 || meta[1] > APP_DATA_MAX_COLUMNS || n > APP_DATA_APP_VALUES)
    return false;
  *columns = meta[1];
  *count = n;
  return true;
}

static void _writeChanges(void) {
  char key[KEY_SIZE];
  if(s_id == NO_ID || (s_dirty == 0 && !s_metaDirty && !s_clearPending))
    return;

  // log_clear(): the old keys first (the meta first, so a power loss leaves no half store)
  if(s_clearPending) {
    _metaKey(key, s_id);
    DataStore_erase(key);
    for(uint8_t n = 0; n < CHUNK_COUNT; n++) {
      _chunkKey(key, s_id, n);
      DataStore_erase(key);
    }
    s_clearPending = false;
  }

  bool good = true;
  for(uint8_t n = 0; n < CHUNK_COUNT; n++) {
    if(!(s_dirty & (1u << n)))
      continue;
    const uint16_t start = (uint16_t)(n * APP_DATA_CHUNK_VALUES);
    if(start >= s_count)
      continue;
    uint16_t part = (uint16_t)(s_count - start);
    if(part > APP_DATA_CHUNK_VALUES)
      part = APP_DATA_CHUNK_VALUES;
    _chunkKey(key, s_id, n);
    if(DataStore_write(key, &s_values[start], (size_t)part * sizeof(int32_t)))
      s_dirty &= (uint16_t)~(1u << n);
    else
      good = false;     // Stays dirty: the next flush tries again
  }
  if(!good || s_count == 0)
    return;

  uint8_t meta[META_SIZE] = { META_VERSION, s_columns, (uint8_t)(s_count & 0xFF), (uint8_t)(s_count >> 8) };
  _metaKey(key, s_id);
  if(DataStore_write(key, meta, META_SIZE)) {
    s_metaDirty = false;
    s_flashCount = s_count;
  }
}

// The values of all apps except `id` (the store in RAM counts with its RAM count)
static uint32_t _otherValues(uint32_t id) {
  uint32_t total = 0;
  for(uint8_t i = 0; i < Apps_getCount(); i++) {
    uint32_t other = 0;
    uint8_t flags = 0;
    if(!Apps_getEntry(i, &other, &flags) || other == id || !Apps_isInstalled(other))
      continue;
    if(other == s_id) {
      total += s_count;
      continue;
    }
    uint8_t columns = 0;
    uint16_t count = 0;
    if(_readMeta(other, &columns, &count))
      total += count;
  }
  return total;
}

// Values that the store of `id` can have now
static uint16_t _capacity(uint32_t id) {
  const uint32_t others = _otherValues(id);
  const uint32_t left = (others < APP_DATA_TOTAL_VALUES) ? APP_DATA_TOTAL_VALUES - others : 0;
  return (uint16_t)((left < APP_DATA_APP_VALUES) ? left : APP_DATA_APP_VALUES);
}

// Chunks for `values` values
static uint32_t _chunks(uint32_t values) {
  return (values + APP_DATA_CHUNK_VALUES - 1) / APP_DATA_CHUNK_VALUES;
}

static void _metaKey(char* key, uint32_t id) {
  snprintf(key, KEY_SIZE, "m%08lx", (unsigned long)id);
}

static void _chunkKey(char* key, uint32_t id, uint8_t chunk) {
  snprintf(key, KEY_SIZE, "c%08lx%02x", (unsigned long)id, (unsigned)chunk);
}
