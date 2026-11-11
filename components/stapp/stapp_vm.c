/***************************************************************************************************
 * @file    : stapp_vm.c
 * @brief   : The app engine: runs the code of a checked package (docs/APPS.md, section 4).
 *
 * Limits at run time: STAPP_MAX_STEPS instructions for one event, STAPP_STACK_SIZE values on the
 * stack, STAPP_CALL_DEPTH calls. A broken limit stops the app with an error; the firmware then
 * shows "APP ERROR" and goes back to the app menu. The engine never reads outside the package.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "stapp.h"
#include "stapp_private.h"

#include <string.h>

/* > Local Types **********************************************************************************/
typedef struct sys_def_s {
  int8_t argCount;      // -1: no such call
  uint16_t permission;
} sys_def_t;

/* > Local Variables ******************************************************************************/
#define SYS_TABLE_SIZE  25

static const sys_def_t kSys[SYS_TABLE_SIZE] = {
  [STAPP_SYS__PIXEL]       = { 3, 0 },
  [STAPP_SYS__HLINE]       = { 4, 0 },
  [STAPP_SYS__VLINE]       = { 4, 0 },
  [STAPP_SYS__RECT]        = { 5, 0 },
  [STAPP_SYS__FILL]        = { 5, 0 },
  [STAPP_SYS__RRECT]       = { 5, 0 },
  [STAPP_SYS__TEXT]        = { 6, 0 },
  [STAPP_SYS__NUM]         = { 7, 0 },
  [STAPP_SYS__BIGNUM]      = { 6, 0 },
  [STAPP_SYS__BITMAP]      = { 4, 0 },
  [STAPP_SYS__INVERT]      = { 4, 0 },
  [11] = { -1, 0 }, [12] = { -1, 0 }, [13] = { -1, 0 }, [14] = { -1, 0 }, [15] = { -1, 0 },
  [STAPP_SYS__COUNTER]     = { 1, 0 },
  [STAPP_SYS__ADD_COUNTER] = { 2, STAPP_PERM_COUNTERS },
  [STAPP_SYS__SETTING]     = { 1, 0 },
  [STAPP_SYS__TIME]        = { 0, 0 },
  [STAPP_SYS__CLICK]       = { 1, STAPP_PERM_SOUND },
  [STAPP_SYS__BATTERY]     = { 0, 0 },
  [STAPP_SYS__RANDOM]      = { 1, 0 },
  [STAPP_SYS__SAVE]        = { 0, 0 },
  [STAPP_SYS__REDRAW]      = { 0, 0 },
};

/* > Function Definitions *************************************************************************/

int8_t Stapp_sysArgCount(uint8_t sysId) {
  return (sysId < SYS_TABLE_SIZE) ? kSys[sysId].argCount : -1;
}

uint16_t Stapp_sysPermission(uint8_t sysId) {
  return (sysId < SYS_TABLE_SIZE) ? kSys[sysId].permission : 0;
}

void Stapp_load(stapp_vm_t* vm, const uint8_t* package, const stapp_info_t* info) {
  memset(vm, 0, sizeof(*vm));
  vm->package = package;
  vm->info = *info;
  vm->error = STAPP_OK;
}

stapp_result_t Stapp_run(stapp_vm_t* vm, stapp_event_t event, stapp_sys_fn_t sys, void* ctx) {
  if(vm == NULL || vm->package == NULL || (unsigned)event >= STAPP_EVENT_COUNT)
    return STAPP_ERR_ARG;
  if(vm->error != STAPP_OK)
    return vm->error;

  const uint8_t* package = vm->package;
  const uint16_t entry = Stapp_rd16(package + STAPP_HANDLERS_OFFSET + 2u * event);
  if(entry == STAPP_NO_HANDLER)
    return STAPP_OK;

  const uint8_t* code = package + Stapp_rd16(package + 44);
  const uint16_t codeSize = Stapp_rd16(package + 46);
  const bool drawing = (event == STAPP_EVENT__DRAW);
  int32_t* const stack = vm->stack;
  int32_t* const globals = vm->globals;
  const uint8_t globalCount = vm->info.globalCount;

  uint16_t pc = entry;
  uint8_t sp = 0;         // Count of values on the stack
  uint8_t depth = 0;      // Count of open calls
  uint16_t steps = 0;
  stapp_result_t error = STAPP_OK;

#define NEED(_n)    do { if(sp < (_n)) { error = STAPP_ERR_STACK; goto fail; } } while(0)
#define ROOM(_n)    do { if(sp + (_n) > STAPP_STACK_SIZE) { error = STAPP_ERR_STACK; goto fail; } } while(0)
// The value first, then sp changes: DUP pushes stack[sp - 1]
#define PUSH(_v)    do { ROOM(1); const int32_t pushed_ = (int32_t)(_v); stack[sp] = pushed_; sp++; } while(0)
#define BINARY(_expr) do { NEED(2); const int32_t a = stack[sp - 2], b = stack[sp - 1]; (void)a; (void)b; \
                            sp--; stack[sp - 1] = (int32_t)(_expr); } while(0)

  while(pc < codeSize) {
    if(++steps > STAPP_MAX_STEPS) {
      error = STAPP_ERR_STEPS;
      goto fail;
    }

    const uint8_t op = code[pc];
    const int8_t len = Stapp_opLength(op);
    if(len < 0 || pc + (uint32_t)len > codeSize) {
      error = STAPP_ERR_CODE;   // Stapp_check() does not let this happen
      goto fail;
    }
    const uint16_t next = (uint16_t)(pc + len);

    switch(op) {
      case STAPP_OP__HALT:
        return STAPP_OK;

      case STAPP_OP__PUSH8:  PUSH((int8_t)code[pc + 1]); break;
      case STAPP_OP__PUSH16: PUSH((int16_t)Stapp_rd16(code + pc + 1)); break;
      case STAPP_OP__PUSH32: PUSH((int32_t)Stapp_rd32(code + pc + 1)); break;

      case STAPP_OP__LOAD:
        if(code[pc + 1] >= globalCount) { error = STAPP_ERR_GLOBAL; goto fail; }
        PUSH(globals[code[pc + 1]]);
        break;
      case STAPP_OP__STORE:
        if(code[pc + 1] >= globalCount) { error = STAPP_ERR_GLOBAL; goto fail; }
        NEED(1);
        globals[code[pc + 1]] = stack[--sp];
        break;

      case STAPP_OP__DUP:  NEED(1); PUSH(stack[sp - 1]); break;
      case STAPP_OP__DROP: NEED(1); sp--; break;
      case STAPP_OP__SWAP: {
        NEED(2);
        const int32_t t = stack[sp - 1];
        stack[sp - 1] = stack[sp - 2];
        stack[sp - 2] = t;
      } break;

      // Arithmetic wraps around (unsigned math, then back to signed)
      case STAPP_OP__ADD: BINARY((uint32_t)a + (uint32_t)b); break;
      case STAPP_OP__SUB: BINARY((uint32_t)a - (uint32_t)b); break;
      case STAPP_OP__MUL: BINARY((uint32_t)a * (uint32_t)b); break;
      case STAPP_OP__DIV: BINARY((b == 0) ? 0 : (a == INT32_MIN && b == -1) ? INT32_MIN : a / b); break;
      case STAPP_OP__MOD: BINARY((b == 0 || b == -1) ? 0 : a % b); break;
      case STAPP_OP__NEG: NEED(1); stack[sp - 1] = (int32_t)(0u - (uint32_t)stack[sp - 1]); break;

      case STAPP_OP__EQ: BINARY(a == b); break;
      case STAPP_OP__NE: BINARY(a != b); break;
      case STAPP_OP__LT: BINARY(a < b); break;
      case STAPP_OP__LE: BINARY(a <= b); break;
      case STAPP_OP__GT: BINARY(a > b); break;
      case STAPP_OP__GE: BINARY(a >= b); break;
      case STAPP_OP__NOT: NEED(1); stack[sp - 1] = (stack[sp - 1] == 0); break;

      case STAPP_OP__BAND: BINARY(a & b); break;
      case STAPP_OP__BOR:  BINARY(a | b); break;
      case STAPP_OP__BXOR: BINARY(a ^ b); break;
      case STAPP_OP__SHL:  BINARY((uint32_t)a << (b & 31)); break;
      case STAPP_OP__SHR:  BINARY((a < 0) ? ~(~(uint32_t)a >> (b & 31)) : ((uint32_t)a >> (b & 31))); break;
      case STAPP_OP__MIN:  BINARY((a < b) ? a : b); break;
      case STAPP_OP__MAX:  BINARY((a > b) ? a : b); break;
      case STAPP_OP__ABS:
        NEED(1);
        if(stack[sp - 1] < 0)
          stack[sp - 1] = (int32_t)(0u - (uint32_t)stack[sp - 1]);
        break;

      case STAPP_OP__JMP:
        pc = (uint16_t)((int32_t)next + (int16_t)Stapp_rd16(code + pc + 1));
        continue;
      case STAPP_OP__JZ:
      case STAPP_OP__JNZ: {
        NEED(1);
        const bool zero = (stack[--sp] == 0);
        if(zero == (op == STAPP_OP__JZ)) {
          pc = (uint16_t)((int32_t)next + (int16_t)Stapp_rd16(code + pc + 1));
          continue;
        }
      } break;

      case STAPP_OP__CALL:
        if(depth >= STAPP_CALL_DEPTH) { error = STAPP_ERR_CALLS; goto fail; }
        vm->calls[depth++] = next;
        pc = Stapp_rd16(code + pc + 1);
        continue;
      case STAPP_OP__RET:
        if(depth == 0) { error = STAPP_ERR_CALLS; goto fail; }   // A handler ends with HALT
        pc = vm->calls[--depth];
        continue;

      case STAPP_OP__SYS: {
        const uint8_t id = code[pc + 1];
        const int8_t argc = Stapp_sysArgCount(id);
        if(argc < 0 || (Stapp_sysPermission(id) & ~vm->info.permissions) != 0) {
          error = STAPP_ERR_SYS;
          goto fail;
        }
        NEED((uint8_t)argc);
        sp = (uint8_t)(sp - argc);
        // A string or bitmap that is not in the package is an app error (the third argument)
        if((id == STAPP_SYS__TEXT && (stack[sp + 2] < 0 || stack[sp + 2] >= vm->info.stringCount)) ||
           (id == STAPP_SYS__BITMAP && (stack[sp + 2] < 0 || stack[sp + 2] >= vm->info.bitmapCount))) {
          error = STAPP_ERR_ARG;
          goto fail;
        }
        // The arguments are stack[sp .. sp + argc - 1], the first one pushed first
        const int32_t result = sys ? sys(ctx, id, &stack[sp], drawing) : 0;
        PUSH(result);
      } break;

      default:
        error = STAPP_ERR_CODE;
        goto fail;
    }

    pc = next;
  }

  return STAPP_OK;   // The end of the code is the end of the handler

fail:
  vm->error = error;
  vm->errorPc = pc;
  return error;

#undef NEED
#undef ROOM
#undef PUSH
#undef BINARY
}
