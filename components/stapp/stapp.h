/***************************************************************************************************
 * @file    : stapp.h
 * @brief   : StatTrak app engine: checks an app package (.stapp) and runs its code with limits.
 *
 * The specification is docs/APPS.md. This module has no ESP-IDF headers and no malloc, so it
 * runs in the firmware, in the simulator and in the unit tests. All that an app can do outside
 * the engine goes through one function of the firmware (stapp_sys_fn_t).
 **************************************************************************************************/

#ifndef _STAPP_H
#define _STAPP_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define STAPP_ENGINE_VERSION      1
#define STAPP_FORMAT_VERSION      1

#define STAPP_HEADER_SIZE         48
#define STAPP_PICTURE_OFFSET      48
#define STAPP_PICTURE_W           80
#define STAPP_PICTURE_H           24
#define STAPP_PICTURE_SIZE        (STAPP_PICTURE_W * STAPP_PICTURE_H / 8)   // 240
#define STAPP_HANDLERS_OFFSET     (STAPP_PICTURE_OFFSET + STAPP_PICTURE_SIZE) // 288
#define STAPP_NO_HANDLER          0xFFFF

#define STAPP_MAX_SIZE            4096
#define STAPP_MAX_NAME            14
#define STAPP_MAX_GLOBALS         48
#define STAPP_MAX_SAVED           16
#define STAPP_MAX_SETTINGS        4
#define STAPP_MIN_VALUES          2
#define STAPP_MAX_VALUES          6
#define STAPP_SETTING_NAME_LEN    12
#define STAPP_VALUE_LEN           8
#define STAPP_MAX_STRINGS         32
#define STAPP_MAX_STRING_LEN      20
#define STAPP_MAX_BITMAPS         8
#define STAPP_MAX_CODE            2048
#define STAPP_MAX_STEPS           4000
#define STAPP_STACK_SIZE          32
#define STAPP_CALL_DEPTH          8

// Permissions (header bits)
#define STAPP_PERM_COUNTERS       0x0001
#define STAPP_PERM_SOUND          0x0002
#define STAPP_PERM_ALL            (STAPP_PERM_COUNTERS | STAPP_PERM_SOUND)

/* > Enumerations *********************************************************************************/
typedef enum stapp_event_e {
  STAPP_EVENT__OPEN = 0,
  STAPP_EVENT__DRAW,
  STAPP_EVENT__PRESS_1,
  STAPP_EVENT__PRESS_2,
  STAPP_EVENT__LONG_1,
  STAPP_EVENT__LONG_2,
  STAPP_EVENT__TICK,
  STAPP_EVENT__CLOSE,
  STAPP_EVENT_COUNT,
} stapp_event_t;

// System calls (SYS id). Argument counts: Stapp_sysArgCount().
typedef enum stapp_sys_e {
  STAPP_SYS__PIXEL = 0,
  STAPP_SYS__HLINE = 1,
  STAPP_SYS__VLINE = 2,
  STAPP_SYS__RECT = 3,
  STAPP_SYS__FILL = 4,
  STAPP_SYS__RRECT = 5,
  STAPP_SYS__TEXT = 6,
  STAPP_SYS__NUM = 7,
  STAPP_SYS__BIGNUM = 8,
  STAPP_SYS__BITMAP = 9,
  STAPP_SYS__INVERT = 10,
  STAPP_SYS__COUNTER = 16,
  STAPP_SYS__ADD_COUNTER = 17,
  STAPP_SYS__SETTING = 18,
  STAPP_SYS__TIME = 19,
  STAPP_SYS__CLICK = 20,
  STAPP_SYS__BATTERY = 21,
  STAPP_SYS__RANDOM = 22,
  STAPP_SYS__SAVE = 23,
  STAPP_SYS__REDRAW = 24,
} stapp_sys_t;

// Result of Stapp_check() and Stapp_run(). Values are stable: the web page shows them.
typedef enum stapp_result_e {
  STAPP_OK = 0,
  STAPP_ERR_SIZE,           // Too small, too large, or the size in the header is wrong
  STAPP_ERR_MAGIC,
  STAPP_ERR_VERSION,        // Needs a newer engine or an unknown format
  STAPP_ERR_CRC,
  STAPP_ERR_HEADER,         // Bad id, name, counts or permissions
  STAPP_ERR_LAYOUT,         // A block is outside the package or overlaps
  STAPP_ERR_SETTINGS,
  STAPP_ERR_STRINGS,
  STAPP_ERR_BITMAPS,
  STAPP_ERR_CODE,           // Unknown opcode, operand outside the code, bad jump target
  STAPP_ERR_GLOBAL,         // Global index out of range
  STAPP_ERR_SYS,            // Unknown system call, or a permission that the header does not have
  STAPP_ERR_HANDLER,        // Handler offset is not an instruction start
  // Run-time errors (Stapp_run)
  STAPP_ERR_STACK,          // Stack overflow or underflow
  STAPP_ERR_CALLS,          // Call depth
  STAPP_ERR_STEPS,          // More than STAPP_MAX_STEPS instructions in one event
  STAPP_ERR_ARG,            // A string or bitmap id out of range at run time
  STAPP_RESULT_COUNT,
} stapp_result_t;

/* > Type definitions *****************************************************************************/

// What Stapp_check() finds out about a package. Valid only if the check returned STAPP_OK.
typedef struct stapp_info_s {
  uint32_t id;
  uint16_t version;
  uint16_t size;
  uint16_t permissions;
  char     name[STAPP_MAX_NAME + 1];
  uint8_t  globalCount;
  uint8_t  savedCount;
  uint8_t  settingCount;
  uint8_t  stringCount;
  uint8_t  bitmapCount;
} stapp_info_t;

// The firmware side of a system call. `args` has Stapp_sysArgCount(id) values. `drawing` is
// true only in the draw event; drawing calls must do nothing when it is false. Returns the result
// that the engine pushes. The firmware checks permissions again and the ranges of all values.
typedef int32_t (*stapp_sys_fn_t)(void* ctx, uint8_t sysId, const int32_t* args, bool drawing);

// One running app. No pointers into the engine: it can be a static variable.
typedef struct stapp_vm_s {
  const uint8_t* package;     // The checked package (stays in RAM while the app runs)
  stapp_info_t   info;
  int32_t        globals[STAPP_MAX_GLOBALS];
  stapp_result_t error;       // First run-time error, STAPP_OK if none. After an error, Stapp_run does nothing.
  uint16_t       errorPc;     // Code offset of the error (for the log)
  // Work memory of Stapp_run (here and not on the small stack of the main loop)
  int32_t        stack[STAPP_STACK_SIZE];
  uint16_t       calls[STAPP_CALL_DEPTH];
} stapp_vm_t;

/* > Function Declarations ************************************************************************/

// Checks a full package (section 3 and 4 of docs/APPS.md). `info` may be NULL.
stapp_result_t Stapp_check(const uint8_t* package, uint32_t size, stapp_info_t* info);

// Text of a result, for logs and tests ("ok", "bad crc", ...)
const char*    Stapp_resultText(stapp_result_t result);

// Prepares a VM for a package that passed Stapp_check(). Globals are 0. The firmware then copies
// saved values into globals[0 .. savedCount-1].
void           Stapp_load(stapp_vm_t* vm, const uint8_t* package, const stapp_info_t* info);

// Runs the handler of `event` (nothing if the app has none). Returns STAPP_OK or the run-time
// error; the error stays in vm->error.
stapp_result_t Stapp_run(stapp_vm_t* vm, stapp_event_t event, stapp_sys_fn_t sys, void* ctx);

// Number of arguments of a system call, -1 for an unknown id
int8_t         Stapp_sysArgCount(uint8_t sysId);

// Permission bits that a system call needs (0 = none)
uint16_t       Stapp_sysPermission(uint8_t sysId);

// Package parts (only for a checked package)
const uint8_t* Stapp_picture(const uint8_t* package);      // 240 bytes, Gfx_drawBitmapRaw format, 80 x 24
const char*    Stapp_string(const uint8_t* package, const stapp_info_t* info, uint8_t index);          // NULL if out of range
bool           Stapp_bitmap(const uint8_t* package, const stapp_info_t* info, uint8_t index,
                            uint8_t* width, uint8_t* height, const uint8_t** data);
bool           Stapp_setting(const uint8_t* package, const stapp_info_t* info, uint8_t index,
                             const char** name, uint8_t* valueCount, uint8_t* defaultValue);   // name: 12 chars, not 0-terminated if full
const char*    Stapp_settingValue(const uint8_t* package, const stapp_info_t* info, uint8_t setting,
                                  uint8_t value);           // 8 chars, not 0-terminated if full; NULL if out of range

// CRC-32 (IEEE, the same as zlib.crc32)
uint32_t       Stapp_crc32(const uint8_t* data, uint32_t size);

#endif // _STAPP_H
