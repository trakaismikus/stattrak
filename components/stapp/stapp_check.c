/***************************************************************************************************
 * @file    : stapp_check.c
 * @brief   : The check of an app package (docs/APPS.md, sections 3 and 4).
 *
 * A package comes from the radio. Each byte can be wrong on purpose. The engine runs only a
 * package that passed this check, and it checks the stack, the steps and the ids at run time too.
 * tools/app.py has the same check in Python; tests/test_apps.py compares the two.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "stapp.h"
#include "stapp_private.h"

#include <string.h>

/* > Defines **************************************************************************************/
#define MIN_FIXED_END   (STAPP_HANDLERS_OFFSET + 2 * STAPP_HANDLERS_V1)   // 304: header, picture, handlers of format 1

/* > Local Types **********************************************************************************/
typedef struct block_s {
  uint32_t start;
  uint32_t end;   // One after the last byte
} block_t;

/* > Local Variables ******************************************************************************/
// One bit for each code byte: 1 = an instruction starts here. Static: the main loop stack is small.
static uint8_t s_starts[STAPP_MAX_CODE / 8];

static const char* const kResultText[STAPP_RESULT_COUNT] = {
  [STAPP_OK]           = "ok",
  [STAPP_ERR_SIZE]     = "bad size",
  [STAPP_ERR_MAGIC]    = "bad magic",
  [STAPP_ERR_VERSION]  = "bad version",
  [STAPP_ERR_CRC]      = "bad crc",
  [STAPP_ERR_HEADER]   = "bad header",
  [STAPP_ERR_LAYOUT]   = "bad layout",
  [STAPP_ERR_SETTINGS] = "bad settings",
  [STAPP_ERR_STRINGS]  = "bad strings",
  [STAPP_ERR_BITMAPS]  = "bad bitmaps",
  [STAPP_ERR_CODE]     = "bad code",
  [STAPP_ERR_GLOBAL]   = "bad global",
  [STAPP_ERR_SYS]      = "bad sys",
  [STAPP_ERR_HANDLER]  = "bad handler",
  [STAPP_ERR_STACK]    = "stack",
  [STAPP_ERR_CALLS]    = "call depth",
  [STAPP_ERR_STEPS]    = "too many steps",
  [STAPP_ERR_ARG]      = "bad arg",
};

/* > Local Function Declarations ******************************************************************/
static bool _isNameChar(uint8_t c);
static bool _isTextChar(uint8_t c);
static bool _fixedText(const uint8_t* p, uint8_t len, uint8_t maxChars, bool (*allowed)(uint8_t));
static bool _overlap(const block_t* a, const block_t* b);
static bool _isPush(uint8_t op);
static stapp_result_t _checkCode(const uint8_t* package, const stapp_info_t* info, uint16_t permissions);

/* > Function Definitions *************************************************************************/

stapp_result_t Stapp_check(const uint8_t* package, uint32_t size, stapp_info_t* infoOut) {
  stapp_info_t info;
  memset(&info, 0, sizeof(info));

  if(package == NULL || size < MIN_FIXED_END + 4 || size > STAPP_MAX_SIZE)
    return STAPP_ERR_SIZE;
  if(memcmp(package, "STAP", 4) != 0)
    return STAPP_ERR_MAGIC;
  if(package[4] < 1 || package[4] > STAPP_FORMAT_VERSION || package[5] < 1 || package[5] > STAPP_ENGINE_VERSION)
    return STAPP_ERR_VERSION;
  // Format 2 has 2 more handlers: the data area starts at 308
  info.handlerCount = STAPP_HANDLER_COUNT(package[4]);
  const uint32_t fixedEnd = STAPP_HANDLERS_OFFSET + 2u * info.handlerCount;
  if(size < fixedEnd + 4)
    return STAPP_ERR_SIZE;
  if(Stapp_rd16(package + 14) != size)
    return STAPP_ERR_SIZE;
  if(Stapp_crc32(package, size - 4) != Stapp_rd32(package + size - 4))
    return STAPP_ERR_CRC;

  // Header
  info.permissions  = Stapp_rd16(package + 6);
  info.id           = Stapp_rd32(package + 8);
  info.version      = Stapp_rd16(package + 12);
  info.size         = (uint16_t)size;
  info.globalCount  = package[32];
  info.savedCount   = package[33];
  info.settingCount = package[34];
  info.stringCount  = package[35];
  info.bitmapCount  = package[36];

  // Ids 0 .. 255: 0 and the built-in apps (1, 2, 3; 4 .. 255 are kept for later built-in apps)
  if((info.permissions & ~STAPP_PERM_ALL) != 0 || info.id <= STAPP_RESERVED_IDS || info.id == 0xFFFFFFFFu ||
     info.globalCount > STAPP_MAX_GLOBALS || info.savedCount > STAPP_MAX_SAVED ||
     info.savedCount > info.globalCount || info.settingCount > STAPP_MAX_SETTINGS ||
     info.stringCount > STAPP_MAX_STRINGS || info.bitmapCount > STAPP_MAX_BITMAPS)
    return STAPP_ERR_HEADER;

  // Format 3 (docs/DATA.md section 6): byte 37 = data columns, permissions data and awake.
  // Formats 1 and 2: byte 37 = 0, only the permissions counters and sound.
  if(package[4] >= 3) {
    info.columnCount = package[37];
    const bool data = (info.permissions & STAPP_PERM_DATA) != 0;
    if(info.columnCount > STAPP_MAX_COLUMNS || data != (info.columnCount > 0) || info.columnCount > info.stringCount)
      return STAPP_ERR_HEADER;
  } else if(package[37] != 0 || (info.permissions & ~STAPP_PERM_V2) != 0) {
    return STAPP_ERR_HEADER;
  }

  // Name: 1 to 14 characters, then only zeros
  const uint8_t* name = package + 16;
  if(!_fixedText(name, 16, STAPP_MAX_NAME, _isNameChar))
    return STAPP_ERR_HEADER;
  memcpy(info.name, name, STAPP_MAX_NAME);
  info.name[STAPP_MAX_NAME] = 0;

  // Layout: the 4 blocks are in [fixedEnd, size - 4) and do not overlap (docs/APPS.md 3.1, steps 7 to 9)
  const uint32_t dataEnd = size - 4;
  const uint16_t settingsOffset = Stapp_rd16(package + 38);
  const uint16_t stringsOffset = Stapp_rd16(package + 40);
  const uint16_t bitmapsOffset = Stapp_rd16(package + 42);
  const uint16_t codeOffset = Stapp_rd16(package + 44);
  const uint16_t codeSize = Stapp_rd16(package + 46);

  if(codeSize > STAPP_MAX_CODE)
    return STAPP_ERR_LAYOUT;

  block_t blocks[4];
  uint8_t blockCount = 0;

  if(info.settingCount > 0) {
    uint32_t p = settingsOffset;
    for(uint8_t s = 0; s < info.settingCount; s++) {
      if(p < fixedEnd || p + STAPP_SETTING_NAME_LEN + 2 > dataEnd)
        return STAPP_ERR_LAYOUT;
      const uint8_t count = package[p + STAPP_SETTING_NAME_LEN];
      if(count < STAPP_MIN_VALUES || count > STAPP_MAX_VALUES)
        return STAPP_ERR_SETTINGS;
      p += STAPP_SETTING_NAME_LEN + 2 + (uint32_t)count * STAPP_VALUE_LEN;
    }
    blocks[blockCount++] = (block_t){ settingsOffset, p };
  }
  if(info.stringCount > 0)
    blocks[blockCount++] = (block_t){ stringsOffset, stringsOffset + 2u * info.stringCount };
  if(info.bitmapCount > 0)
    blocks[blockCount++] = (block_t){ bitmapsOffset, bitmapsOffset + 4u * info.bitmapCount };
  if(codeSize > 0)
    blocks[blockCount++] = (block_t){ codeOffset, (uint32_t)codeOffset + codeSize };

  for(uint8_t a = 0; a < blockCount; a++) {
    if(blocks[a].start < fixedEnd || blocks[a].end > dataEnd)
      return STAPP_ERR_LAYOUT;
  }
  for(uint8_t a = 0; a < blockCount; a++) {
    for(uint8_t b = a + 1; b < blockCount; b++) {
      if(_overlap(&blocks[a], &blocks[b]))
        return STAPP_ERR_LAYOUT;
    }
  }

  // Settings: names, defaults, values
  if(info.settingCount > 0) {
    uint32_t p = settingsOffset;
    for(uint8_t s = 0; s < info.settingCount; s++) {
      const uint8_t count = package[p + STAPP_SETTING_NAME_LEN];
      if(!_fixedText(package + p, STAPP_SETTING_NAME_LEN, STAPP_SETTING_NAME_LEN, _isTextChar) ||
         package[p + STAPP_SETTING_NAME_LEN + 1] >= count)
        return STAPP_ERR_SETTINGS;
      p += STAPP_SETTING_NAME_LEN + 2;
      for(uint8_t v = 0; v < count; v++) {
        if(!_fixedText(package + p, STAPP_VALUE_LEN, STAPP_VALUE_LEN, _isTextChar))
          return STAPP_ERR_SETTINGS;
        p += STAPP_VALUE_LEN;
      }
    }
  }

  // Strings: in the data area, a 0 before the CRC, at most STAPP_MAX_STRING_LEN printable characters
  for(uint8_t s = 0; s < info.stringCount; s++) {
    const uint16_t offset = Stapp_rd16(package + stringsOffset + 2u * s);
    if(offset < fixedEnd || offset >= dataEnd)
      return STAPP_ERR_STRINGS;
    uint32_t end = offset;
    while(end < dataEnd && package[end] != 0)
      end++;
    if(end >= dataEnd || end - offset > STAPP_MAX_STRING_LEN)
      return STAPP_ERR_STRINGS;
    for(uint32_t i = offset; i < end; i++) {
      if(!_isTextChar(package[i]))
        return STAPP_ERR_STRINGS;
    }
    // The column names (the first strings): 1 to 12 characters
    if(s < info.columnCount && (end == offset || end - offset > STAPP_COLUMN_NAME_LEN))
      return STAPP_ERR_STRINGS;
  }

  // Bitmaps: size and data in the data area
  for(uint8_t b = 0; b < info.bitmapCount; b++) {
    const uint8_t* entry = package + bitmapsOffset + 4u * b;
    const uint8_t w = entry[0], h = entry[1];
    const uint16_t offset = Stapp_rd16(entry + 2);
    if(w < 1 || w > 128 || h < 1 || h > 32 || offset < fixedEnd ||
       (uint32_t)offset + (uint32_t)w * ((h + 7u) / 8u) > dataEnd)
      return STAPP_ERR_BITMAPS;
  }

  const stapp_result_t code = _checkCode(package, &info, info.permissions);
  if(code != STAPP_OK)
    return code;

  if(infoOut)
    *infoOut = info;
  return STAPP_OK;
}

bool Stapp_isNameField(const uint8_t* field, uint8_t len) {
  return field != NULL && _fixedText(field, len, STAPP_MAX_NAME, _isNameChar);
}

const char* Stapp_resultText(stapp_result_t result) {
  if((unsigned)result >= STAPP_RESULT_COUNT)
    return "unknown";
  return kResultText[result];
}

// --- Package parts (for checked packages)

const uint8_t* Stapp_picture(const uint8_t* package) {
  return package + STAPP_PICTURE_OFFSET;
}

const char* Stapp_string(const uint8_t* package, const stapp_info_t* info, uint8_t index) {
  if(index >= info->stringCount)
    return NULL;
  const uint16_t table = Stapp_rd16(package + 40);
  return (const char*)(package + Stapp_rd16(package + table + 2u * index));
}

bool Stapp_bitmap(const uint8_t* package, const stapp_info_t* info, uint8_t index,
                  uint8_t* width, uint8_t* height, const uint8_t** data) {
  if(index >= info->bitmapCount)
    return false;
  const uint8_t* entry = package + Stapp_rd16(package + 42) + 4u * index;
  *width = entry[0];
  *height = entry[1];
  *data = package + Stapp_rd16(entry + 2);
  return true;
}

// Address of setting `index` in a checked package
static const uint8_t* _settingAt(const uint8_t* package, uint8_t index) {
  const uint8_t* p = package + Stapp_rd16(package + 38);
  for(uint8_t s = 0; s < index; s++)
    p += STAPP_SETTING_NAME_LEN + 2 + p[STAPP_SETTING_NAME_LEN] * STAPP_VALUE_LEN;
  return p;
}

bool Stapp_setting(const uint8_t* package, const stapp_info_t* info, uint8_t index,
                   const char** name, uint8_t* valueCount, uint8_t* defaultValue) {
  if(index >= info->settingCount)
    return false;
  const uint8_t* p = _settingAt(package, index);
  *name = (const char*)p;
  *valueCount = p[STAPP_SETTING_NAME_LEN];
  *defaultValue = p[STAPP_SETTING_NAME_LEN + 1];
  return true;
}

const char* Stapp_settingValue(const uint8_t* package, const stapp_info_t* info, uint8_t setting, uint8_t value) {
  if(setting >= info->settingCount)
    return NULL;
  const uint8_t* p = _settingAt(package, setting);
  if(value >= p[STAPP_SETTING_NAME_LEN])
    return NULL;
  return (const char*)(p + STAPP_SETTING_NAME_LEN + 2 + value * STAPP_VALUE_LEN);
}

uint32_t Stapp_crc32(const uint8_t* data, uint32_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for(uint32_t i = 0; i < size; i++) {
    crc ^= data[i];
    for(uint8_t b = 0; b < 8; b++)
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
  }
  return ~crc;
}

/* > Local Function Definitions *******************************************************************/

// Letters, digits and the signs of a name (docs/APPS.md 3.1, step 6)
static bool _isNameChar(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == '+' || c == '-' || c == '%' || c == '/' || c == '.' || c == ':';
}

// The characters that the firmware fonts have
static bool _isTextChar(uint8_t c) {
  return c >= 0x20 && c <= 0x7E;
}

// A text of fixed length: 1 to maxChars allowed characters, then only zeros
static bool _fixedText(const uint8_t* p, uint8_t len, uint8_t maxChars, bool (*allowed)(uint8_t)) {
  uint8_t n = 0;
  while(n < len && p[n] != 0) {
    if(!allowed(p[n]))
      return false;
    n++;
  }
  if(n == 0 || n > maxChars)
    return false;
  for(uint8_t i = n; i < len; i++) {
    if(p[i] != 0)
      return false;
  }
  return true;
}

static bool _overlap(const block_t* a, const block_t* b) {
  return a->start < b->end && b->start < a->end;
}

static bool _isPush(uint8_t op) {
  return op == STAPP_OP__PUSH8 || op == STAPP_OP__PUSH16 || op == STAPP_OP__PUSH32;
}

static bool _isStart(uint16_t pc) {
  return (s_starts[pc >> 3] >> (pc & 7)) & 1u;
}

// The code, docs/APPS.md 3.1 steps 13 to 16: known opcodes with operands inside the code, variables
// in range, known system calls with their permissions, fixed string / bitmap ids in range, the
// last instruction ends the code, jump and call targets and handlers on an instruction start.
static stapp_result_t _checkCode(const uint8_t* package, const stapp_info_t* info, uint16_t permissions) {
  const uint8_t* code = package + Stapp_rd16(package + 44);
  const uint16_t codeSize = Stapp_rd16(package + 46);

  memset(s_starts, 0, sizeof(s_starts));

  // The last 4 instructions before the current one: opcode and operand (for the fixed-id rule)
  uint8_t prevOp[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
  int32_t prevValue[4] = { 0, 0, 0, 0 };
  uint8_t lastOp = 0xFF;

  uint16_t pc = 0;
  while(pc < codeSize) {
    s_starts[pc >> 3] |= (uint8_t)(1u << (pc & 7));
    const uint8_t op = code[pc];
    const int8_t len = Stapp_opLength(op);
    if(len < 0 || pc + (uint32_t)len > codeSize)
      return STAPP_ERR_CODE;

    int32_t value = 0;
    switch(op) {
      case STAPP_OP__PUSH8:  value = (int8_t)code[pc + 1]; break;
      case STAPP_OP__PUSH16: value = (int16_t)Stapp_rd16(code + pc + 1); break;
      case STAPP_OP__PUSH32: value = (int32_t)Stapp_rd32(code + pc + 1); break;
      case STAPP_OP__LOAD:
      case STAPP_OP__STORE:
        if(code[pc + 1] >= info->globalCount)
          return STAPP_ERR_GLOBAL;
        break;
      case STAPP_OP__SYS: {
        const uint8_t id = code[pc + 1];
        if(Stapp_sysArgCount(id) < 0 || (Stapp_sysPermission(id) & ~permissions) != 0)
          return STAPP_ERR_SYS;
        // A fixed id: the instructions right before the SYS push all arguments from the id on
        if(id == STAPP_SYS__TEXT || id == STAPP_SYS__BITMAP) {
          const uint8_t back = (id == STAPP_SYS__TEXT) ? 4 : 2;
          bool fixed = true;
          for(uint8_t k = 0; k < back; k++)
            fixed = fixed && _isPush(prevOp[k]);
          if(fixed) {
            const int32_t fixedId = prevValue[back - 1];
            const uint8_t count = (id == STAPP_SYS__TEXT) ? info->stringCount : info->bitmapCount;
            if(fixedId < 0 || fixedId >= count)
              return (id == STAPP_SYS__TEXT) ? STAPP_ERR_STRINGS : STAPP_ERR_BITMAPS;
          }
        }
      } break;
      default: break;
    }

    for(uint8_t k = 3; k > 0; k--) {
      prevOp[k] = prevOp[k - 1];
      prevValue[k] = prevValue[k - 1];
    }
    prevOp[0] = op;
    prevValue[0] = value;
    lastOp = op;
    pc = (uint16_t)(pc + len);
  }

  // The code cannot run past its end
  if(codeSize > 0 && lastOp != STAPP_OP__HALT && lastOp != STAPP_OP__JMP && lastOp != STAPP_OP__RET)
    return STAPP_ERR_CODE;

  // Jump and call targets
  pc = 0;
  while(pc < codeSize) {
    const uint8_t op = code[pc];
    const int8_t len = Stapp_opLength(op);
    int32_t target = -1;
    bool jumps = true;

    if(op == STAPP_OP__JMP || op == STAPP_OP__JZ || op == STAPP_OP__JNZ)
      target = (int32_t)pc + len + (int16_t)Stapp_rd16(code + pc + 1);
    else if(op == STAPP_OP__CALL)
      target = Stapp_rd16(code + pc + 1);
    else
      jumps = false;

    if(jumps && (target < 0 || target >= codeSize || !_isStart((uint16_t)target)))
      return STAPP_ERR_CODE;
    pc = (uint16_t)(pc + len);
  }

  // Handlers
  for(uint8_t e = 0; e < info->handlerCount; e++) {
    const uint16_t entry = Stapp_rd16(package + STAPP_HANDLERS_OFFSET + 2u * e);
    if(entry != STAPP_NO_HANDLER && (entry >= codeSize || !_isStart(entry)))
      return STAPP_ERR_HANDLER;
  }

  return STAPP_OK;
}
