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
#define FIXED_END   (STAPP_HANDLERS_OFFSET + 2 * STAPP_EVENT_COUNT)   // 304: header, picture, handlers

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
  [STAPP_ERR_MAGIC]    = "not an app",
  [STAPP_ERR_VERSION]  = "needs a newer firmware",
  [STAPP_ERR_CRC]      = "bad crc",
  [STAPP_ERR_HEADER]   = "bad header",
  [STAPP_ERR_LAYOUT]   = "bad layout",
  [STAPP_ERR_SETTINGS] = "bad settings",
  [STAPP_ERR_STRINGS]  = "bad strings",
  [STAPP_ERR_BITMAPS]  = "bad bitmaps",
  [STAPP_ERR_CODE]     = "bad code",
  [STAPP_ERR_GLOBAL]   = "bad variable",
  [STAPP_ERR_SYS]      = "bad system call",
  [STAPP_ERR_HANDLER]  = "bad handler",
  [STAPP_ERR_STACK]    = "stack error",
  [STAPP_ERR_CALLS]    = "too many calls",
  [STAPP_ERR_STEPS]    = "too many steps",
  [STAPP_ERR_ARG]      = "bad argument",
};

/* > Local Function Declarations ******************************************************************/
static bool _isNameChar(uint8_t c);
static bool _isTextChar(uint8_t c);
static bool _fixedText(const uint8_t* p, uint8_t len, bool (*allowed)(uint8_t));
static bool _inData(uint32_t start, uint32_t len, uint32_t dataEnd);
static bool _overlap(const block_t* a, const block_t* b);
static stapp_result_t _checkCode(const uint8_t* package, const stapp_info_t* info, uint16_t permissions);

/* > Function Definitions *************************************************************************/

stapp_result_t Stapp_check(const uint8_t* package, uint32_t size, stapp_info_t* infoOut) {
  stapp_info_t info;
  memset(&info, 0, sizeof(info));

  if(package == NULL || size < FIXED_END + 4 || size > STAPP_MAX_SIZE)
    return STAPP_ERR_SIZE;
  if(memcmp(package, "STAP", 4) != 0)
    return STAPP_ERR_MAGIC;
  if(package[4] != STAPP_FORMAT_VERSION || package[5] < 1 || package[5] > STAPP_ENGINE_VERSION)
    return STAPP_ERR_VERSION;
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

  if((info.permissions & ~STAPP_PERM_ALL) != 0 || info.id == 0 || info.id == 0xFFFFFFFFu ||
     info.globalCount > STAPP_MAX_GLOBALS || info.savedCount > STAPP_MAX_SAVED ||
     info.savedCount > info.globalCount || info.settingCount > STAPP_MAX_SETTINGS ||
     info.stringCount > STAPP_MAX_STRINGS || info.bitmapCount > STAPP_MAX_BITMAPS || package[37] != 0)
    return STAPP_ERR_HEADER;

  // Name: 1 to 14 characters, then only zeros
  const uint8_t* name = package + 16;
  uint8_t nameLen = 0;
  while(nameLen < 16 && name[nameLen] != 0)
    nameLen++;
  if(nameLen < 1 || nameLen > STAPP_MAX_NAME)
    return STAPP_ERR_HEADER;
  for(uint8_t i = 0; i < 16; i++) {
    if((i < nameLen && !_isNameChar(name[i])) || (i >= nameLen && name[i] != 0))
      return STAPP_ERR_HEADER;
  }
  memcpy(info.name, name, nameLen);
  info.name[nameLen] = 0;

  // Layout: the blocks are in [FIXED_END, size - 4) and do not overlap
  const uint32_t dataEnd = size - 4;
  const uint16_t settingsOffset = Stapp_rd16(package + 38);
  const uint16_t stringsOffset = Stapp_rd16(package + 40);
  const uint16_t bitmapsOffset = Stapp_rd16(package + 42);
  const uint16_t codeOffset = Stapp_rd16(package + 44);
  const uint16_t codeSize = Stapp_rd16(package + 46);

  if(codeSize < 1 || codeSize > STAPP_MAX_CODE || !_inData(codeOffset, codeSize, dataEnd))
    return STAPP_ERR_LAYOUT;

  block_t blocks[4];
  uint8_t blockCount = 0;
  blocks[blockCount++] = (block_t){ codeOffset, (uint32_t)codeOffset + codeSize };

  // Settings: walk through them to find the size of the block
  if(info.settingCount > 0) {
    uint32_t p = settingsOffset;
    for(uint8_t s = 0; s < info.settingCount; s++) {
      if(!_inData(p, STAPP_SETTING_NAME_LEN + 2, dataEnd))
        return STAPP_ERR_LAYOUT;
      const uint8_t count = package[p + STAPP_SETTING_NAME_LEN];
      const uint8_t def = package[p + STAPP_SETTING_NAME_LEN + 1];
      if(!_fixedText(package + p, STAPP_SETTING_NAME_LEN, _isTextChar) ||
         count < STAPP_MIN_VALUES || count > STAPP_MAX_VALUES || def >= count)
        return STAPP_ERR_SETTINGS;
      p += STAPP_SETTING_NAME_LEN + 2;
      if(!_inData(p, (uint32_t)count * STAPP_VALUE_LEN, dataEnd))
        return STAPP_ERR_LAYOUT;
      for(uint8_t v = 0; v < count; v++) {
        if(!_fixedText(package + p, STAPP_VALUE_LEN, _isTextChar))
          return STAPP_ERR_SETTINGS;
        p += STAPP_VALUE_LEN;
      }
    }
    blocks[blockCount++] = (block_t){ settingsOffset, p };
  }

  if(info.stringCount > 0) {
    if(!_inData(stringsOffset, 2u * info.stringCount, dataEnd))
      return STAPP_ERR_LAYOUT;
    blocks[blockCount++] = (block_t){ stringsOffset, stringsOffset + 2u * info.stringCount };
  }

  if(info.bitmapCount > 0) {
    if(!_inData(bitmapsOffset, 4u * info.bitmapCount, dataEnd))
      return STAPP_ERR_LAYOUT;
    blocks[blockCount++] = (block_t){ bitmapsOffset, bitmapsOffset + 4u * info.bitmapCount };
  }

  for(uint8_t a = 0; a < blockCount; a++) {
    for(uint8_t b = a + 1; b < blockCount; b++) {
      if(_overlap(&blocks[a], &blocks[b]))
        return STAPP_ERR_LAYOUT;
    }
  }

  // Strings: in the data area, 0-terminated, at most STAPP_MAX_STRING_LEN characters
  for(uint8_t s = 0; s < info.stringCount; s++) {
    const uint16_t offset = Stapp_rd16(package + stringsOffset + 2u * s);
    if(!_inData(offset, 1, dataEnd))
      return STAPP_ERR_STRINGS;
    uint32_t len = 0;
    while(offset + len < dataEnd && package[offset + len] != 0) {
      if(!_isTextChar(package[offset + len]) || len >= STAPP_MAX_STRING_LEN)
        return STAPP_ERR_STRINGS;
      len++;
    }
    if(offset + len >= dataEnd)
      return STAPP_ERR_STRINGS;   // No terminating 0
  }

  // Bitmaps: size and data in the data area
  for(uint8_t b = 0; b < info.bitmapCount; b++) {
    const uint8_t* entry = package + bitmapsOffset + 4u * b;
    const uint8_t w = entry[0], h = entry[1];
    const uint16_t offset = Stapp_rd16(entry + 2);
    if(w < 1 || w > 128 || h < 1 || h > 32 || !_inData(offset, (uint32_t)w * ((h + 7u) / 8u), dataEnd))
      return STAPP_ERR_BITMAPS;
  }

  const stapp_result_t code = _checkCode(package, &info, info.permissions);
  if(code != STAPP_OK)
    return code;

  if(infoOut)
    *infoOut = info;
  return STAPP_OK;
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

// Letters, digits and the signs of a name (APPS.md, section 2). Lower case is allowed: it is shown in upper case.
static bool _isNameChar(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == '+' || c == '-' || c == '%' || c == '/' || c == '.' || c == ':';
}

// The characters that the firmware fonts have
static bool _isTextChar(uint8_t c) {
  return c >= 0x20 && c <= 0x7E;
}

// A text of fixed length: at least one character, then only zeros
static bool _fixedText(const uint8_t* p, uint8_t len, bool (*allowed)(uint8_t)) {
  uint8_t n = 0;
  while(n < len && p[n] != 0) {
    if(!allowed(p[n]))
      return false;
    n++;
  }
  if(n == 0)
    return false;
  for(uint8_t i = n; i < len; i++) {
    if(p[i] != 0)
      return false;
  }
  return true;
}

static bool _inData(uint32_t start, uint32_t len, uint32_t dataEnd) {
  return start >= FIXED_END && start <= dataEnd && len <= dataEnd - start;
}

static bool _overlap(const block_t* a, const block_t* b) {
  return a->start < b->end && b->start < a->end;
}

static bool _isStart(uint16_t pc) {
  return (s_starts[pc >> 3] >> (pc & 7)) & 1u;
}

// Decodes all code: known opcodes, operands inside the code, jump and call targets on an
// instruction start, variables in range, known system calls with their permissions.
static stapp_result_t _checkCode(const uint8_t* package, const stapp_info_t* info, uint16_t permissions) {
  const uint8_t* code = package + Stapp_rd16(package + 44);
  const uint16_t codeSize = Stapp_rd16(package + 46);

  memset(s_starts, 0, sizeof(s_starts));

  // Pass 1: instruction starts and operands
  uint16_t pc = 0;
  while(pc < codeSize) {
    s_starts[pc >> 3] |= (uint8_t)(1u << (pc & 7));
    const int8_t len = Stapp_opLength(code[pc]);
    if(len < 0 || pc + (uint32_t)len > codeSize)
      return STAPP_ERR_CODE;

    switch(code[pc]) {
      case STAPP_OP__LOAD:
      case STAPP_OP__STORE:
        if(code[pc + 1] >= info->globalCount)
          return STAPP_ERR_GLOBAL;
        break;
      case STAPP_OP__SYS: {
        const uint8_t id = code[pc + 1];
        if(Stapp_sysArgCount(id) < 0 || (Stapp_sysPermission(id) & ~permissions) != 0)
          return STAPP_ERR_SYS;
      } break;
      default: break;
    }
    pc = (uint16_t)(pc + len);
  }

  // Pass 2: jump and call targets
  pc = 0;
  while(pc < codeSize) {
    const uint8_t op = code[pc];
    const int8_t len = Stapp_opLength(op);
    int32_t target = -1;

    if(op == STAPP_OP__JMP || op == STAPP_OP__JZ || op == STAPP_OP__JNZ)
      target = (int32_t)pc + len + (int16_t)Stapp_rd16(code + pc + 1);
    else if(op == STAPP_OP__CALL)
      target = Stapp_rd16(code + pc + 1);

    if(op == STAPP_OP__JMP || op == STAPP_OP__JZ || op == STAPP_OP__JNZ || op == STAPP_OP__CALL) {
      if(target < 0 || target >= codeSize || !_isStart((uint16_t)target))
        return STAPP_ERR_CODE;
    }
    pc = (uint16_t)(pc + len);
  }

  // Handlers
  for(uint8_t e = 0; e < STAPP_EVENT_COUNT; e++) {
    const uint16_t entry = Stapp_rd16(package + STAPP_HANDLERS_OFFSET + 2u * e);
    if(entry != STAPP_NO_HANDLER && (entry >= codeSize || !_isStart(entry)))
      return STAPP_ERR_HANDLER;
  }

  return STAPP_OK;
}
