/***************************************************************************************************
 * @file    : app_data.c
 * @brief   : The data store of each app. See app_data.h and docs/DATA.md section 2.
 *
 * Flash ("data" namespace): "m<id>" = u8 version 1, u8 columns, u16 value count; "c<id><n>" =
 * chunk n (64 values, the last one can be shorter). Chunks are written before the meta, so a
 * power loss between them keeps the old count (the data is not damaged). Everything from flash
 * is checked: a wrong meta = no data, a missing or short chunk = the data ends before it.
 *
 * RAM: two stores (slots). The app on the screen uses slot FG, the background app slot BG (one
 * app that is both uses only BG: the app host runs it once). An app id is never in two slots.
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

/* > Local Types **********************************************************************************/
typedef struct store_s {
  int32_t  values[APP_DATA_APP_VALUES];
  uint32_t id;               // The store in RAM (NO_ID = none)
  uint8_t  columns;          // Of the store (0 = empty store)
  uint16_t count;            // Values
  uint16_t dirty;            // Chunks to write (bit n = chunk n)
  bool     metaDirty;
  uint32_t nextFlush;
  bool     flushNow;         // The app closed: write in the next AppData_update()
  bool     clearPending;     // log_clear(): the flash keys of id go in the next write
  uint16_t flashCount;       // Values of id that are in flash (for the reserve check)
  uint16_t reclaim;          // NVS entries that the pending clear frees (for the reserve check)

  // The app that runs with this store (app host)
  bool     appOpen;
  uint8_t  appColumns;
  uint32_t flashOthers;      // Values of the apps that are in no slot (from flash)
  uint32_t othersGen;        // s_gen when flashOthers was counted
} store_t;

/* > Local Function Declarations ******************************************************************/
static store_t* _slot(uint8_t slot);
static store_t* _find(uint32_t id);
static store_t* _forRead(uint32_t id);
static void _reset(store_t* s);
static void _load(store_t* s, uint32_t id);
static uint32_t _chunks(uint32_t values);
static uint32_t _pendingChunks(const store_t* s, uint32_t values);
static bool _readMeta(uint32_t id, uint8_t* columns, uint16_t* count);
static void _writeChanges(store_t* s);
static uint32_t _otherValues(uint32_t id);
static uint32_t _flashOthers(void);
static uint16_t _capacity(uint32_t id);
static void _metaKey(char* key, uint32_t id);
static void _chunkKey(char* key, uint32_t id, uint8_t chunk);

/* > Local Variables ******************************************************************************/
static store_t  s_slots[APP_DATA_SLOTS];
static uint32_t s_gen = 1;     // +1 each time a slot gets an other id: the flashOthers counts are old

/* > Function Definitions *************************************************************************/

void AppData_open(uint8_t slot, uint32_t id, uint8_t columns) {
  store_t* s = _slot(slot);
  if(s == NULL)
    return;
  s->appOpen = false;
  if(columns == 0 || columns > APP_DATA_MAX_COLUMNS || id == NO_ID)
    return;

  // The store of `id` in the other slot (the sync page read it): to flash, and out of that slot.
  // If the other slot runs this app, this slot does not open it (one id is never in two slots).
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    store_t* other = &s_slots[i];
    if(other == s || other->id != id)
      continue;
    if(other->appOpen)
      return;
    _writeChanges(other);
    _reset(other);
    s_gen++;
  }

  _load(s, id);
  s->appOpen = true;
  s->appColumns = columns;
  s->othersGen = 0;            // Count flashOthers at the first log()
}

void AppData_closeLater(uint8_t slot) {
  store_t* s = _slot(slot);
  if(s == NULL)
    return;
  if(s->appOpen && (s->dirty != 0 || s->metaDirty || s->clearPending))
    s->flushNow = true;
  s->appOpen = false;
}

bool AppData_log(uint8_t slot, const int32_t* values) {
  store_t* s = _slot(slot);
  if(s == NULL || !s->appOpen || values == NULL)
    return false;
  // A store of an other column count (an older version of the app) takes no rows until it is cleared
  const uint8_t columns = (s->count == 0) ? s->appColumns : s->columns;
  if(columns != s->appColumns)
    return false;

  // All apps together: the apps in no slot (flash, counted again when a slot changed) and the RAM
  // count of the other slot
  if(s->othersGen != s_gen) {
    s->flashOthers = _flashOthers();
    s->othersGen = s_gen;
  }
  uint32_t others = s->flashOthers;
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    const store_t* other = &s_slots[i];
    if(other != s && other->id != NO_ID && other->id != s->id && Apps_isInstalled(other->id))
      others += other->count;
  }
  const uint32_t left = (others < APP_DATA_TOTAL_VALUES) ? APP_DATA_TOTAL_VALUES - others : 0;
  if((uint32_t)s->count + columns > left || (uint32_t)s->count + columns > APP_DATA_APP_VALUES)
    return false;

  // A new chunk needs flash: the settings and the counters keep APP_DATA_RESERVE_ENTRIES. The chunks
  // that are only in RAM, or only partly in flash (they grow when they are written), need it too:
  // the ones of this store and the ones of the other slot.
  const uint32_t chunksNow = _chunks(s->count);
  const uint32_t chunksAfter = _chunks((uint32_t)s->count + columns);
  if(chunksAfter > chunksNow) {
    uint32_t pending = _pendingChunks(s, (uint32_t)s->count + columns);
    uint32_t reclaim = 0;
    for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
      const store_t* other = &s_slots[i];
      reclaim += other->reclaim;
      // The other slot: its chunks can grow while its app runs, and wait for a write while dirty
      if(other != s && other->id != NO_ID && (other->appOpen || other->dirty != 0 || other->metaDirty))
        pending += _pendingChunks(other, other->count);
    }
    if(DataStore_availableEntries() + reclaim < APP_DATA_RESERVE_ENTRIES + CHUNK_ENTRIES * pending)
      return false;
  }

  for(uint8_t c = 0; c < columns; c++) {
    s->values[s->count] = values[c];
    s->dirty |= (uint16_t)(1u << (s->count / APP_DATA_CHUNK_VALUES));
    s->count++;
  }
  s->columns = columns;
  s->metaDirty = true;
  return true;
}

uint16_t AppData_rows(uint8_t slot) {
  const store_t* s = _slot(slot);
  return (s != NULL && s->appOpen && s->columns > 0) ? (uint16_t)(s->count / s->columns) : 0;
}

int32_t AppData_get(uint8_t slot, int32_t row, int32_t column) {
  const store_t* s = _slot(slot);
  // The row first: a large row number must not overflow the index
  if(s == NULL || !s->appOpen || s->columns == 0 || row < 0 || row >= s->count / s->columns ||
     column < 0 || column >= s->columns)
    return 0;
  return s->values[row * s->columns + column];
}

// log_clear() can run in an app handler that runs while a view draws (the close of an app, the
// open of the first app after the start picture): only RAM here, the flash keys go in AppData_update()
void AppData_clear(uint8_t slot) {
  store_t* s = _slot(slot);
  if(s == NULL || !s->appOpen)
    return;
  // The entries of the old store in flash are free after the deferred erase (meta + chunks, about).
  // A second clear before the erase frees nothing more: the flash still has the same old store.
  if(!s->clearPending && s->flashCount > 0) {
    const uint32_t bytes = (uint32_t)s->flashCount * sizeof(int32_t);
    uint32_t reclaim = 3u + (bytes / (APP_DATA_CHUNK_VALUES * 4u)) * 10u;
    if(bytes % (APP_DATA_CHUNK_VALUES * 4u))
      reclaim += 2u + ((bytes % (APP_DATA_CHUNK_VALUES * 4u)) + 31u) / 32u;
    s->reclaim = (uint16_t)reclaim;
  }
  s->count = 0;
  s->columns = 0;
  s->dirty = 0;
  s->metaDirty = false;
  s->flashCount = 0;
  s->clearPending = true;
}

void AppData_update(uint32_t now) {
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    store_t* s = &s_slots[i];
    if(s->clearPending)
      s->flushNow = true;    // A clear goes to flash at once (no 30 s wait)
    if(s->dirty == 0 && !s->metaDirty && !s->clearPending) {
      s->flushNow = false;
      continue;
    }
    if(!s->flushNow && (int32_t)(now - s->nextFlush) < 0)
      continue;
    s->flushNow = false;
    _writeChanges(s);
    s->nextFlush = now + APP_DATA_FLUSH_MS;
  }
}

void AppData_flush(void) {
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++)
    _writeChanges(&s_slots[i]);
}

bool AppData_info(uint32_t id, uint8_t* columns, uint16_t* rows) {
  if(!Apps_isInstalled(id))
    return false;
  const store_t* s = _forRead(id);
  if(s == NULL)
    return false;
  if(columns)
    *columns = s->columns;
  if(rows)
    *rows = (s->columns > 0) ? (uint16_t)(s->count / s->columns) : 0;
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
  const store_t* s = _forRead(id);
  if(s == NULL)
    return 0;
  uint8_t count = 0;
  while(count < max && (uint32_t)first + count < s->count) {
    out[count] = s->values[first + count];
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
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    store_t* s = &s_slots[i];
    if(s->id != id)
      continue;
    s->count = 0;
    s->columns = 0;
    s->dirty = 0;
    s->metaDirty = false;
    s->clearPending = false;
    s->flashCount = 0;
    s->reclaim = 0;
  }
  s_gen++;
}

uint32_t AppData_usedBytes(void) {
  uint32_t values = _otherValues(NO_ID);
  return values * 4u;
}

void AppData_forget(void) {
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    _reset(&s_slots[i]);
    s_slots[i].appOpen = false;
  }
  s_gen++;
}

/* > Local Function Definitions *******************************************************************/

static store_t* _slot(uint8_t slot) {
  return (slot < APP_DATA_SLOTS) ? &s_slots[slot] : NULL;
}

static store_t* _find(uint32_t id) {
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    if(s_slots[i].id == id)
      return &s_slots[i];
  }
  return NULL;
}

// The store of `id` for the sync page: the slot that has it, else a slot with no running app
static store_t* _forRead(uint32_t id) {
  store_t* s = _find(id);
  if(s != NULL)
    return s;
  for(uint8_t i = 0; i < APP_DATA_SLOTS; i++) {
    if(!s_slots[i].appOpen) {
      _load(&s_slots[i], id);
      return &s_slots[i];
    }
  }
  return NULL;
}

static void _reset(store_t* s) {
  s->id = NO_ID;
  s->count = 0;
  s->columns = 0;
  s->dirty = 0;
  s->metaDirty = false;
  s->flushNow = false;
  s->clearPending = false;
  s->flashCount = 0;
  s->reclaim = 0;
}

// The store of `id` into the slot. The store that is there now goes to flash first.
static void _load(store_t* s, uint32_t id) {
  char key[KEY_SIZE];
  if(id == s->id)
    return;
  _writeChanges(s);

  _reset(s);
  s->id = id;
  s_gen++;

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
    if(!DataStore_readAny(key, &s->values[loaded], APP_DATA_CHUNK_VALUES * sizeof(int32_t), &length) ||
       length < (size_t)part * sizeof(int32_t))
      break;     // Missing or short: the data ends here
    loaded = (uint16_t)(loaded + part);
  }
  // Only whole rows
  s->count = (uint16_t)(loaded - (loaded % columns));
  s->columns = (s->count > 0) ? columns : 0;
  s->flashCount = s->count;
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

static void _writeChanges(store_t* s) {
  char key[KEY_SIZE];
  if(s->id == NO_ID || (s->dirty == 0 && !s->metaDirty && !s->clearPending))
    return;

  // log_clear(): the old keys first (the meta first, so a power loss leaves no half store)
  if(s->clearPending) {
    _metaKey(key, s->id);
    DataStore_erase(key);
    for(uint8_t n = 0; n < CHUNK_COUNT; n++) {
      _chunkKey(key, s->id, n);
      DataStore_erase(key);
    }
    s->clearPending = false;
    s->reclaim = 0;
  }

  bool good = true;
  for(uint8_t n = 0; n < CHUNK_COUNT; n++) {
    if(!(s->dirty & (1u << n)))
      continue;
    const uint16_t start = (uint16_t)(n * APP_DATA_CHUNK_VALUES);
    if(start >= s->count)
      continue;
    uint16_t part = (uint16_t)(s->count - start);
    if(part > APP_DATA_CHUNK_VALUES)
      part = APP_DATA_CHUNK_VALUES;
    _chunkKey(key, s->id, n);
    if(DataStore_write(key, &s->values[start], (size_t)part * sizeof(int32_t)))
      s->dirty &= (uint16_t)~(1u << n);
    else
      good = false;     // Stays dirty: the next flush tries again
  }
  if(!good || s->count == 0)
    return;

  uint8_t meta[META_SIZE] = { META_VERSION, s->columns, (uint8_t)(s->count & 0xFF), (uint8_t)(s->count >> 8) };
  _metaKey(key, s->id);
  if(DataStore_write(key, meta, META_SIZE)) {
    s->metaDirty = false;
    s->flashCount = s->count;
  }
}

// The values of all apps except `id` (a store in a slot counts with its RAM count)
static uint32_t _otherValues(uint32_t id) {
  uint32_t total = 0;
  for(uint8_t i = 0; i < Apps_getCount(); i++) {
    uint32_t other = 0;
    uint8_t flags = 0;
    if(!Apps_getEntry(i, &other, &flags) || other == id || !Apps_isInstalled(other))
      continue;
    const store_t* s = _find(other);
    if(s != NULL) {
      total += s->count;
      continue;
    }
    uint8_t columns = 0;
    uint16_t count = 0;
    if(_readMeta(other, &columns, &count))
      total += count;
  }
  return total;
}

// The values of the installed apps that are in no slot (from flash)
static uint32_t _flashOthers(void) {
  uint32_t total = 0;
  for(uint8_t i = 0; i < Apps_getCount(); i++) {
    uint32_t other = 0;
    uint8_t flags = 0;
    if(!Apps_getEntry(i, &other, &flags) || !Apps_isInstalled(other) || _find(other) != NULL)
      continue;
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

// Chunks of a store with `values` values that still need new flash: all but the full ones in flash
static uint32_t _pendingChunks(const store_t* s, uint32_t values) {
  const uint32_t chunks = _chunks(values);
  const uint32_t fullInFlash = s->flashCount / APP_DATA_CHUNK_VALUES;
  return (chunks > fullInFlash) ? chunks - fullInFlash : 0;
}

static void _metaKey(char* key, uint32_t id) {
  snprintf(key, KEY_SIZE, "m%08lx", (unsigned long)id);
}

static void _chunkKey(char* key, uint32_t id, uint8_t chunk) {
  snprintf(key, KEY_SIZE, "c%08lx%02x", (unsigned long)id, (unsigned)chunk);
}
