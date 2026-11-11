/***************************************************************************************************
 * @file    : test_stapp.c
 * @brief   : Unit tests of the app engine (components/stapp): the package check, the bytecode
 *            and the limits, and a fuzz test with many broken packages.
 *
 * A package comes from the radio, so the check and the engine must be safe for any bytes.
 * Run with the memory checker (Linux, CI) to find reads outside the package.
 **************************************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "utest.h"
#include "stapp.h"

/* > A small package builder ***********************************************************************/

enum { OP_HALT = 0x00, OP_PUSH8 = 0x01, OP_PUSH16 = 0x02, OP_PUSH32 = 0x03, OP_LOAD = 0x04,
       OP_STORE = 0x05, OP_DUP = 0x06, OP_DROP = 0x07, OP_SWAP = 0x08, OP_ADD = 0x10, OP_SUB = 0x11,
       OP_MUL = 0x12, OP_DIV = 0x13, OP_MOD = 0x14, OP_NEG = 0x15, OP_EQ = 0x16, OP_LT = 0x18,
       OP_NOT = 0x1C, OP_SHR = 0x21, OP_MIN = 0x22, OP_ABS = 0x24, OP_JMP = 0x30, OP_JZ = 0x31,
       OP_JNZ = 0x32, OP_CALL = 0x33, OP_RET = 0x34, OP_SYS = 0x40 };

typedef struct {
  uint8_t data[STAPP_MAX_SIZE];
  uint32_t size;
} pkg_t;

typedef struct {
  const uint8_t* code;
  uint16_t codeSize;
  uint16_t handlers[STAPP_EVENT_COUNT];
  uint8_t globals;
  uint8_t saved;
  uint16_t permissions;
  const char* name;
  const char* strings[4];
  uint8_t stringCount;
  bool withSetting;
  bool withBitmap;
} spec_t;

static void _put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void _put32(uint8_t* p, uint32_t v) { _put16(p, v); _put16(p + 2, v >> 16); }

static void _finish(pkg_t* pkg) {
  _put16(pkg->data + 14, pkg->size);
  _put32(pkg->data + pkg->size - 4, Stapp_crc32(pkg->data, pkg->size - 4));
}

static void _build(pkg_t* pkg, const spec_t* spec) {
  memset(pkg, 0, sizeof(*pkg));
  uint8_t* d = pkg->data;
  memcpy(d, "STAP", 4);
  d[4] = STAPP_FORMAT_VERSION;
  d[5] = 1;
  _put16(d + 6, spec->permissions);
  _put32(d + 8, 0x12345678);
  _put16(d + 12, 3);
  const char* name = spec->name ? spec->name : "TEST APP";
  memcpy(d + 16, name, strlen(name));
  d[32] = spec->globals;
  d[33] = spec->saved;
  d[34] = spec->withSetting ? 1 : 0;
  d[35] = spec->stringCount;
  d[36] = spec->withBitmap ? 1 : 0;
  for(int i = 0; i < STAPP_PICTURE_SIZE; i++)
    d[STAPP_PICTURE_OFFSET + i] = (uint8_t)i;
  for(int e = 0; e < STAPP_EVENT_COUNT; e++)
    _put16(d + STAPP_HANDLERS_OFFSET + 2 * e, spec->handlers[e]);

  uint32_t p = STAPP_HANDLERS_OFFSET + 2 * STAPP_EVENT_COUNT;   // 304
  if(spec->withSetting) {
    _put16(d + 38, p);
    memcpy(d + p, "ROUND TIME", 10);
    d[p + 12] = 3;       // values
    d[p + 13] = 1;       // default
    p += 14;
    memcpy(d + p, "1:00", 4); p += 8;
    memcpy(d + p, "1:30", 4); p += 8;
    memcpy(d + p, "2:00", 4); p += 8;
  }
  if(spec->stringCount) {
    _put16(d + 40, p);
    uint32_t table = p;
    p += 2u * spec->stringCount;
    for(uint8_t s = 0; s < spec->stringCount; s++) {
      _put16(d + table + 2u * s, p);
      const size_t len = strlen(spec->strings[s]);
      memcpy(d + p, spec->strings[s], len);
      p += (uint32_t)len + 1;
    }
  }
  if(spec->withBitmap) {
    _put16(d + 42, p);
    d[p] = 8; d[p + 1] = 8; _put16(d + p + 2, p + 4);
    p += 4;
    for(int i = 0; i < 8; i++) d[p++] = 0xFF;
  }
  _put16(d + 44, p);
  _put16(d + 46, spec->codeSize);
  memcpy(d + p, spec->code, spec->codeSize);
  p += spec->codeSize;
  pkg->size = p + 4;
  _finish(pkg);
}

static spec_t _spec(const uint8_t* code, uint16_t size) {
  spec_t s;
  memset(&s, 0, sizeof(s));
  s.code = code;
  s.codeSize = size;
  for(int e = 0; e < STAPP_EVENT_COUNT; e++)
    s.handlers[e] = STAPP_NO_HANDLER;
  s.handlers[STAPP_EVENT__OPEN] = 0;
  s.globals = 4;
  return s;
}

/* > A system call stand-in that records the calls ***********************************************/

typedef struct {
  int calls;
  uint8_t lastId;
  int32_t lastArgs[8];
  bool lastDrawing;
  int32_t result;
} sys_log_t;

static int32_t _sys(void* ctx, uint8_t id, const int32_t* args, bool drawing) {
  sys_log_t* log = (sys_log_t*)ctx;
  const int8_t argc = Stapp_sysArgCount(id);
  log->calls++;
  log->lastId = id;
  log->lastDrawing = drawing;
  for(int i = 0; i < argc && i < 8; i++)
    log->lastArgs[i] = args[i];
  return log->result;
}

// Runs `code` as the open handler; returns the result, globals in *vm
static stapp_result_t _runCode(const uint8_t* code, uint16_t size, stapp_vm_t* vm, sys_log_t* log) {
  static pkg_t pkg;
  spec_t spec = _spec(code, size);
  spec.permissions = STAPP_PERM_ALL;
  _build(&pkg, &spec);
  stapp_info_t info;
  const stapp_result_t check = Stapp_check(pkg.data, pkg.size, &info);
  if(check != STAPP_OK)
    return check;
  Stapp_load(vm, pkg.data, &info);
  return Stapp_run(vm, STAPP_EVENT__OPEN, _sys, log);
}

/* > The check ************************************************************************************/

static const uint8_t kHalt[] = { OP_HALT };

TEST(check_accepts_a_small_package) {
  pkg_t pkg;
  spec_t spec = _spec(kHalt, sizeof(kHalt));
  spec.saved = 2;
  spec.withSetting = true;
  spec.withBitmap = true;
  spec.strings[0] = "ROUND"; spec.strings[1] = "GO!"; spec.stringCount = 2;
  _build(&pkg, &spec);

  stapp_info_t info;
  CHECK_EQ(Stapp_check(pkg.data, pkg.size, &info), STAPP_OK);
  CHECK_EQ(info.id, 0x12345678);
  CHECK_EQ(info.version, 3);
  CHECK_EQ(info.size, pkg.size);
  CHECK(strcmp(info.name, "TEST APP") == 0);
  CHECK_EQ(info.globalCount, 4);
  CHECK_EQ(info.savedCount, 2);
  CHECK_EQ(info.settingCount, 1);
  CHECK(strcmp(Stapp_string(pkg.data, &info, 1), "GO!") == 0);
  CHECK(Stapp_string(pkg.data, &info, 2) == NULL);

  const char* name; uint8_t count, def;
  CHECK(Stapp_setting(pkg.data, &info, 0, &name, &count, &def));
  CHECK(strncmp(name, "ROUND TIME", 10) == 0);
  CHECK_EQ(count, 3);
  CHECK_EQ(def, 1);
  CHECK(strncmp(Stapp_settingValue(pkg.data, &info, 0, 2), "2:00", 4) == 0);
  CHECK(Stapp_settingValue(pkg.data, &info, 0, 3) == NULL);
  CHECK(!Stapp_setting(pkg.data, &info, 1, &name, &count, &def));

  uint8_t w, h; const uint8_t* bits;
  CHECK(Stapp_bitmap(pkg.data, &info, 0, &w, &h, &bits));
  CHECK_EQ(w, 8);
  CHECK_EQ(bits[0], 0xFF);
  CHECK_EQ(Stapp_picture(pkg.data)[5], 5);
}

TEST(crc32_is_the_zlib_crc) {
  CHECK_EQ(Stapp_crc32((const uint8_t*)"123456789", 9), 0xCBF43926u);
}

// Builds a good package, changes it with `edit`, fixes the CRC (unless told not to) and checks it
static stapp_result_t _checkEdited(void (*edit)(pkg_t*), bool fixCrc) {
  static pkg_t pkg;
  spec_t spec = _spec(kHalt, sizeof(kHalt));
  spec.withSetting = true;
  spec.strings[0] = "AB"; spec.stringCount = 1;
  _build(&pkg, &spec);
  edit(&pkg);
  if(fixCrc)
    _finish(&pkg);
  return Stapp_check(pkg.data, pkg.size, NULL);
}

static void _edMagic(pkg_t* p)        { p->data[0] = 'X'; }
static void _edFormat(pkg_t* p)       { p->data[4] = 2; }
static void _edEngine(pkg_t* p)       { p->data[5] = STAPP_ENGINE_VERSION + 1; }
static void _edCrc(pkg_t* p)          { p->data[STAPP_PICTURE_OFFSET] ^= 1; }
static void _edSizeField(pkg_t* p)    { _put16(p->data + 14, p->size + 1); }
static void _edId(pkg_t* p)           { _put32(p->data + 8, 0); }
static void _edPerm(pkg_t* p)         { _put16(p->data + 6, 0x8000); }
static void _edNameEmpty(pkg_t* p)    { memset(p->data + 16, 0, 16); }
static void _edNameChar(pkg_t* p)     { p->data[17] = '#'; }
static void _edNameTail(pkg_t* p)     { p->data[30] = 'X'; }
static void _edSaved(pkg_t* p)        { p->data[33] = 5; }
static void _edGlobals(pkg_t* p)      { p->data[32] = STAPP_MAX_GLOBALS + 1; }
static void _edReserved(pkg_t* p)     { p->data[37] = 1; }
static void _edCodeOut(pkg_t* p)      { _put16(p->data + 46, 3000); }
static void _edCodeInHeader(pkg_t* p) { _put16(p->data + 44, 10); }
static void _edOverlap(pkg_t* p)      { _put16(p->data + 40, 304); }   // String table on the setting
static void _edSettingCount(pkg_t* p) { p->data[304 + 12] = 1; }
static void _edSettingDefault(pkg_t* p) { p->data[304 + 13] = 3; }
static void _edSettingName(pkg_t* p)  { memset(p->data + 304, 0, 12); }
// The string "AB" is the last data before the 1-byte code: replace its 0 and the code byte
static void _edStringNoEnd(pkg_t* p)  { uint16_t s = (uint16_t)(p->data[40] | (p->data[41] << 8));
                                        uint16_t o = (uint16_t)(p->data[s] | (p->data[s + 1] << 8));
                                        p->data[o + 2] = 'C'; p->data[o + 3] = 'D'; }
static void _edStringOut(pkg_t* p)    { uint16_t s = (uint16_t)(p->data[40] | (p->data[41] << 8)); _put16(p->data + s, 4000); }
static void _edTooSmall(pkg_t* p)     { p->size = 100; }

TEST(check_finds_each_kind_of_broken_package) {
  CHECK_EQ(_checkEdited(_edMagic, true), STAPP_ERR_MAGIC);
  CHECK_EQ(_checkEdited(_edFormat, true), STAPP_ERR_VERSION);
  CHECK_EQ(_checkEdited(_edEngine, true), STAPP_ERR_VERSION);
  CHECK_EQ(_checkEdited(_edCrc, false), STAPP_ERR_CRC);
  CHECK_EQ(_checkEdited(_edSizeField, false), STAPP_ERR_SIZE);
  CHECK_EQ(_checkEdited(_edTooSmall, false), STAPP_ERR_SIZE);
  CHECK_EQ(_checkEdited(_edId, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edPerm, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edNameEmpty, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edNameChar, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edNameTail, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edSaved, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edGlobals, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edReserved, true), STAPP_ERR_HEADER);
  CHECK_EQ(_checkEdited(_edCodeOut, true), STAPP_ERR_LAYOUT);
  CHECK_EQ(_checkEdited(_edCodeInHeader, true), STAPP_ERR_LAYOUT);
  CHECK_EQ(_checkEdited(_edOverlap, true), STAPP_ERR_LAYOUT);
  CHECK_EQ(_checkEdited(_edSettingCount, true), STAPP_ERR_SETTINGS);
  CHECK_EQ(_checkEdited(_edSettingDefault, true), STAPP_ERR_SETTINGS);
  CHECK_EQ(_checkEdited(_edSettingName, true), STAPP_ERR_SETTINGS);
  CHECK_EQ(_checkEdited(_edStringNoEnd, true), STAPP_ERR_STRINGS);
  CHECK_EQ(_checkEdited(_edStringOut, true), STAPP_ERR_STRINGS);
  CHECK_EQ(Stapp_check(NULL, 400, NULL), STAPP_ERR_SIZE);
}

static stapp_result_t _checkCode(const uint8_t* code, uint16_t size, uint16_t permissions, uint16_t openHandler) {
  static pkg_t pkg;
  spec_t spec = _spec(code, size);
  spec.permissions = permissions;
  spec.handlers[STAPP_EVENT__OPEN] = openHandler;
  _build(&pkg, &spec);
  return Stapp_check(pkg.data, pkg.size, NULL);
}

TEST(check_finds_bad_code) {
  const uint8_t unknownOp[] = { 0x99 };
  const uint8_t cutOperand[] = { OP_PUSH32, 1, 2 };
  const uint8_t jumpIntoOperand[] = { OP_PUSH16, 1, 0, OP_JMP, 0xFC, 0xFF };   // to pc 2
  const uint8_t jumpOut[] = { OP_JMP, 0x10, 0x00 };
  const uint8_t callOut[] = { OP_CALL, 0x00, 0x01 };
  const uint8_t badGlobal[] = { OP_LOAD, 4, OP_HALT };
  const uint8_t badStore[] = { OP_PUSH8, 1, OP_STORE, 9, OP_HALT };
  const uint8_t unknownSys[] = { OP_SYS, 11, OP_HALT };
  const uint8_t soundSys[] = { OP_PUSH16, 0xE8, 0x03, OP_SYS, 20, OP_HALT };
  const uint8_t counterSys[] = { OP_PUSH8, 2, OP_PUSH8, 1, OP_SYS, 17, OP_HALT };
  const uint8_t good[] = { OP_PUSH8, 1, OP_JZ, 0x00, 0x00, OP_HALT };

  CHECK_EQ(_checkCode(unknownOp, sizeof(unknownOp), 0, 0), STAPP_ERR_CODE);
  CHECK_EQ(_checkCode(cutOperand, sizeof(cutOperand), 0, 0), STAPP_ERR_CODE);
  CHECK_EQ(_checkCode(jumpIntoOperand, sizeof(jumpIntoOperand), 0, 0), STAPP_ERR_CODE);
  CHECK_EQ(_checkCode(jumpOut, sizeof(jumpOut), 0, 0), STAPP_ERR_CODE);
  CHECK_EQ(_checkCode(callOut, sizeof(callOut), 0, 0), STAPP_ERR_CODE);
  CHECK_EQ(_checkCode(badGlobal, sizeof(badGlobal), 0, 0), STAPP_ERR_GLOBAL);
  CHECK_EQ(_checkCode(badStore, sizeof(badStore), 0, 0), STAPP_ERR_GLOBAL);
  CHECK_EQ(_checkCode(unknownSys, sizeof(unknownSys), 0, 0), STAPP_ERR_SYS);
  CHECK_EQ(_checkCode(soundSys, sizeof(soundSys), 0, 0), STAPP_ERR_SYS);
  CHECK_EQ(_checkCode(soundSys, sizeof(soundSys), STAPP_PERM_SOUND, 0), STAPP_OK);
  CHECK_EQ(_checkCode(counterSys, sizeof(counterSys), STAPP_PERM_SOUND, 0), STAPP_ERR_SYS);
  CHECK_EQ(_checkCode(counterSys, sizeof(counterSys), STAPP_PERM_COUNTERS, 0), STAPP_OK);
  CHECK_EQ(_checkCode(good, sizeof(good), 0, 0), STAPP_OK);
  CHECK_EQ(_checkCode(good, sizeof(good), 0, 1), STAPP_ERR_HANDLER);   // Inside PUSH8
  CHECK_EQ(_checkCode(good, sizeof(good), 0, 6), STAPP_ERR_HANDLER);   // After the end
  CHECK_EQ(_checkCode(good, sizeof(good), 0, STAPP_NO_HANDLER), STAPP_OK);
}

/* > The engine ***********************************************************************************/

TEST(engine_arithmetic_and_variables) {
  const uint8_t code[] = {
    OP_PUSH8, 7, OP_PUSH8, 5, OP_SUB, OP_STORE, 0,             // g0 = 2
    OP_PUSH16, 0x10, 0x27, OP_PUSH8, 3, OP_DIV, OP_STORE, 1,   // g1 = 10000 / 3
    OP_PUSH8, 0xF9, OP_PUSH8, 3, OP_MOD, OP_STORE, 2,          // g2 = -7 % 3 = -1
    OP_PUSH8, 0xF8, OP_PUSH8, 1, OP_SHR, OP_ABS, OP_NEG, OP_PUSH8, 2, OP_MIN, OP_STORE, 3,  // g3 = min(-4, 2)
    OP_HALT };
  stapp_vm_t vm; sys_log_t log = { 0 };
  CHECK_EQ(_runCode(code, sizeof(code), &vm, &log), STAPP_OK);
  CHECK_EQ(vm.globals[0], 2);
  CHECK_EQ(vm.globals[1], 3333);
  CHECK_EQ(vm.globals[2], -1);
  CHECK_EQ(vm.globals[3], -4);
}

TEST(engine_division_by_zero_and_overflow_are_safe) {
  const uint8_t code[] = {
    OP_PUSH8, 9, OP_PUSH8, 0, OP_DIV, OP_STORE, 0,
    OP_PUSH8, 9, OP_PUSH8, 0, OP_MOD, OP_STORE, 1,
    OP_PUSH32, 0x00, 0x00, 0x00, 0x80, OP_PUSH8, 0xFF, OP_DIV, OP_STORE, 2,   // INT32_MIN / -1
    OP_PUSH32, 0xFF, 0xFF, 0xFF, 0x7F, OP_PUSH8, 1, OP_ADD, OP_STORE, 3,      // INT32_MAX + 1 wraps
    OP_HALT };
  stapp_vm_t vm; sys_log_t log = { 0 };
  CHECK_EQ(_runCode(code, sizeof(code), &vm, &log), STAPP_OK);
  CHECK_EQ(vm.globals[0], 0);
  CHECK_EQ(vm.globals[1], 0);
  CHECK_EQ(vm.globals[2], INT32_MIN);
  CHECK_EQ(vm.globals[3], INT32_MIN);
}

TEST(engine_loop_and_call) {
  // g0 = 0; while(g0 < 10) g0 = g0 + 1; then call a function that sets g1 = 42
  const uint8_t code[] = {
    /* 0 */ OP_LOAD, 0, OP_PUSH8, 10, OP_LT, OP_JZ, 0x0A, 0x00,      // to 18
    /* 8 */ OP_LOAD, 0, OP_PUSH8, 1, OP_ADD, OP_STORE, 0, OP_JMP, 0xEE, 0xFF,  // to 0
    /* 18 */ OP_CALL, 22, 0x00, OP_HALT,
    /* 22 */ OP_PUSH8, 42, OP_STORE, 1, OP_RET };
  stapp_vm_t vm; sys_log_t log = { 0 };
  CHECK_EQ(_runCode(code, sizeof(code), &vm, &log), STAPP_OK);
  CHECK_EQ(vm.globals[0], 10);
  CHECK_EQ(vm.globals[1], 42);
}

TEST(engine_system_call_gets_its_arguments_in_order) {
  // text(x=3, y=4, str=0, font=1, c=1, align=2) in the open handler: not drawing
  const uint8_t code[] = { OP_PUSH8, 3, OP_PUSH8, 4, OP_PUSH8, 0, OP_PUSH8, 1, OP_PUSH8, 1, OP_PUSH8, 2,
                           OP_SYS, 6, OP_STORE, 0, OP_HALT };
  stapp_vm_t vm; sys_log_t log = { 0 };
  log.result = 77;
  CHECK_EQ(_runCode(code, sizeof(code), &vm, &log), STAPP_OK);
  CHECK_EQ(log.calls, 1);
  CHECK_EQ(log.lastId, 6);
  CHECK_EQ(log.lastArgs[0], 3);
  CHECK_EQ(log.lastArgs[5], 2);
  CHECK(!log.lastDrawing);
  CHECK_EQ(vm.globals[0], 77);
}

TEST(engine_drawing_flag_only_in_draw) {
  static pkg_t pkg;
  const uint8_t code[] = { OP_PUSH8, 1, OP_PUSH8, 1, OP_PUSH8, 1, OP_SYS, 0, OP_DROP, OP_HALT };
  spec_t spec = _spec(code, sizeof(code));
  spec.handlers[STAPP_EVENT__DRAW] = 0;
  _build(&pkg, &spec);
  stapp_info_t info;
  CHECK_EQ(Stapp_check(pkg.data, pkg.size, &info), STAPP_OK);
  stapp_vm_t vm; sys_log_t log = { 0 };
  Stapp_load(&vm, pkg.data, &info);
  CHECK_EQ(Stapp_run(&vm, STAPP_EVENT__DRAW, _sys, &log), STAPP_OK);
  CHECK(log.lastDrawing);
  CHECK_EQ(Stapp_run(&vm, STAPP_EVENT__OPEN, _sys, &log), STAPP_OK);
  CHECK(!log.lastDrawing);
  CHECK_EQ(Stapp_run(&vm, STAPP_EVENT__TICK, _sys, &log), STAPP_OK);   // No handler: nothing
  CHECK_EQ(log.calls, 2);
}

TEST(engine_limits_stop_the_app) {
  stapp_vm_t vm; sys_log_t log = { 0 };
  const uint8_t endless[] = { OP_JMP, 0xFD, 0xFF };
  CHECK_EQ(_runCode(endless, sizeof(endless), &vm, &log), STAPP_ERR_STEPS);
  CHECK_EQ(vm.error, STAPP_ERR_STEPS);
  CHECK_EQ(Stapp_run(&vm, STAPP_EVENT__OPEN, _sys, &log), STAPP_ERR_STEPS);   // Stays stopped

  const uint8_t pushForever[] = { OP_PUSH8, 1, OP_JMP, 0xFB, 0xFF };
  CHECK_EQ(_runCode(pushForever, sizeof(pushForever), &vm, &log), STAPP_ERR_STACK);

  const uint8_t underflow[] = { OP_DROP, OP_HALT };
  CHECK_EQ(_runCode(underflow, sizeof(underflow), &vm, &log), STAPP_ERR_STACK);

  const uint8_t sysUnderflow[] = { OP_SYS, 3, OP_HALT };   // rect needs 5 arguments
  CHECK_EQ(_runCode(sysUnderflow, sizeof(sysUnderflow), &vm, &log), STAPP_ERR_STACK);

  const uint8_t recursion[] = { OP_CALL, 0x00, 0x00 };
  CHECK_EQ(_runCode(recursion, sizeof(recursion), &vm, &log), STAPP_ERR_CALLS);

  const uint8_t retFromHandler[] = { OP_PUSH8, 5, OP_STORE, 0, OP_RET, OP_PUSH8, 6, OP_STORE, 0 };
  CHECK_EQ(_runCode(retFromHandler, sizeof(retFromHandler), &vm, &log), STAPP_OK);
  CHECK_EQ(vm.globals[0], 5);

  const uint8_t offTheEnd[] = { OP_PUSH8, 9, OP_STORE, 0 };
  CHECK_EQ(_runCode(offTheEnd, sizeof(offTheEnd), &vm, &log), STAPP_OK);
  CHECK_EQ(vm.globals[0], 9);
}

TEST(engine_sys_table) {
  CHECK_EQ(Stapp_sysArgCount(STAPP_SYS__NUM), 7);
  CHECK_EQ(Stapp_sysArgCount(STAPP_SYS__TIME), 0);
  CHECK_EQ(Stapp_sysArgCount(11), -1);
  CHECK_EQ(Stapp_sysArgCount(200), -1);
  CHECK_EQ(Stapp_sysPermission(STAPP_SYS__ADD_COUNTER), STAPP_PERM_COUNTERS);
  CHECK_EQ(Stapp_sysPermission(STAPP_SYS__CLICK), STAPP_PERM_SOUND);
  CHECK_EQ(Stapp_sysPermission(STAPP_SYS__COUNTER), 0);
  CHECK(strcmp(Stapp_resultText(STAPP_ERR_CRC), "bad crc") == 0);
  CHECK(strcmp(Stapp_resultText((stapp_result_t)99), "unknown") == 0);
}

/* > Fuzz: random changes to a good package must never crash the check or the engine ************/

static uint32_t s_rng = 12345;
static uint32_t _rand(void) {
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  return s_rng;
}

static int32_t _fuzzSys(void* ctx, uint8_t id, const int32_t* args, bool drawing) {
  (void)ctx; (void)drawing;
  const int8_t argc = Stapp_sysArgCount(id);
  int32_t sum = 0;
  for(int i = 0; i < argc; i++)
    sum += args[i];   // Reads each argument (the memory checker finds a wrong pointer)
  return (int32_t)(_rand() & 0xFF) + (sum & 1);
}

TEST(fuzz_broken_packages_are_safe) {
  // A package with many kinds of instructions
  const uint8_t code[] = {
    OP_LOAD, 0, OP_PUSH8, 3, OP_LT, OP_JZ, 0x0A, 0x00,
    OP_LOAD, 0, OP_PUSH8, 1, OP_ADD, OP_STORE, 0, OP_JMP, 0xEE, 0xFF,
    OP_PUSH8, 1, OP_PUSH8, 2, OP_PUSH8, 0, OP_PUSH8, 1, OP_PUSH8, 1, OP_PUSH8, 0, OP_SYS, 6, OP_DROP,
    OP_CALL, 39, 0x00, OP_SYS, 19, OP_HALT,
    OP_PUSH8, 2, OP_SYS, 16, OP_STORE, 1, OP_RET };
  static pkg_t good, pkg;
  spec_t spec = _spec(code, sizeof(code));
  spec.withSetting = true;
  spec.withBitmap = true;
  spec.strings[0] = "ROUND"; spec.stringCount = 1;
  for(int e = 0; e < STAPP_EVENT_COUNT; e++)
    spec.handlers[e] = 0;
  _build(&good, &spec);
  CHECK_EQ(Stapp_check(good.data, good.size, NULL), STAPP_OK);

  static stapp_vm_t vm;
  int passed = 0, ran = 0;
  for(int round = 0; round < 30000; round++) {
    pkg = good;
    const int changes = 1 + (int)(_rand() % 4);
    for(int c = 0; c < changes; c++) {
      const uint32_t at = _rand() % good.size;
      switch(_rand() % 3) {
        case 0: pkg.data[at] = (uint8_t)_rand(); break;
        case 1: pkg.data[at] ^= (uint8_t)(1u << (_rand() % 8)); break;
        default: pkg.data[at] = (uint8_t)((_rand() & 1) ? 0x00 : 0xFF); break;
      }
    }
    if(_rand() % 8 == 0)
      pkg.size = _rand() % (good.size + 16);
    else
      _finish(&pkg);   // A good CRC lets the check go further

    stapp_info_t info;
    if(Stapp_check(pkg.data, pkg.size, &info) != STAPP_OK)
      continue;
    passed++;
    Stapp_load(&vm, pkg.data, &info);
    for(int e = 0; e < STAPP_EVENT_COUNT; e++) {
      Stapp_run(&vm, (stapp_event_t)e, _fuzzSys, NULL);
      ran++;
    }
    // Package parts of a checked package are safe to read
    for(uint8_t s = 0; s < info.stringCount; s++)
      CHECK(Stapp_string(pkg.data, &info, s) != NULL);
  }
  CHECK(passed > 100);   // Enough broken packages passed the check to test the engine
  CHECK(ran > 0);
  printf("fuzz: %d of 30000 changed packages passed the check\n", passed);
}

int main(void) {
  RUN(check_accepts_a_small_package);
  RUN(crc32_is_the_zlib_crc);
  RUN(check_finds_each_kind_of_broken_package);
  RUN(check_finds_bad_code);
  RUN(engine_arithmetic_and_variables);
  RUN(engine_division_by_zero_and_overflow_are_safe);
  RUN(engine_loop_and_call);
  RUN(engine_system_call_gets_its_arguments_in_order);
  RUN(engine_drawing_flag_only_in_draw);
  RUN(engine_limits_stop_the_app);
  RUN(engine_sys_table);
  RUN(fuzz_broken_packages_are_safe);
  return utest_finish();
}
