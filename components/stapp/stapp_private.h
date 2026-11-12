/***************************************************************************************************
 * @file    : stapp_private.h
 * @brief   : Parts of the app engine that only the engine uses: opcodes and byte reading.
 **************************************************************************************************/

#ifndef _STAPP_PRIVATE_H
#define _STAPP_PRIVATE_H

/* > Includes *************************************************************************************/
#include <stdint.h>

/* > Enumerations *********************************************************************************/
// Opcodes (docs/APPS.md, section 4)
typedef enum stapp_op_e {
  STAPP_OP__HALT   = 0x00,
  STAPP_OP__PUSH8  = 0x01,
  STAPP_OP__PUSH16 = 0x02,
  STAPP_OP__PUSH32 = 0x03,
  STAPP_OP__LOAD   = 0x04,
  STAPP_OP__STORE  = 0x05,
  STAPP_OP__DUP    = 0x06,
  STAPP_OP__DROP   = 0x07,
  STAPP_OP__SWAP   = 0x08,
  STAPP_OP__ADD    = 0x10,
  STAPP_OP__SUB    = 0x11,
  STAPP_OP__MUL    = 0x12,
  STAPP_OP__DIV    = 0x13,
  STAPP_OP__MOD    = 0x14,
  STAPP_OP__NEG    = 0x15,
  STAPP_OP__EQ     = 0x16,
  STAPP_OP__NE     = 0x17,
  STAPP_OP__LT     = 0x18,
  STAPP_OP__LE     = 0x19,
  STAPP_OP__GT     = 0x1A,
  STAPP_OP__GE     = 0x1B,
  STAPP_OP__NOT    = 0x1C,
  STAPP_OP__BAND   = 0x1D,
  STAPP_OP__BOR    = 0x1E,
  STAPP_OP__BXOR   = 0x1F,
  STAPP_OP__SHL    = 0x20,
  STAPP_OP__SHR    = 0x21,
  STAPP_OP__MIN    = 0x22,
  STAPP_OP__MAX    = 0x23,
  STAPP_OP__ABS    = 0x24,
  STAPP_OP__JMP    = 0x30,
  STAPP_OP__JZ     = 0x31,
  STAPP_OP__JNZ    = 0x32,
  STAPP_OP__CALL   = 0x33,
  STAPP_OP__RET    = 0x34,
  STAPP_OP__SYS    = 0x40,
} stapp_op_t;

/* > Inline functions *****************************************************************************/
static inline uint16_t Stapp_rd16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t Stapp_rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Length of an instruction in bytes (opcode + operands), -1 for an unknown opcode
static inline int8_t Stapp_opLength(uint8_t op) {
  switch(op) {
    case STAPP_OP__PUSH8: case STAPP_OP__LOAD: case STAPP_OP__STORE: case STAPP_OP__SYS:
      return 2;
    case STAPP_OP__PUSH16: case STAPP_OP__JMP: case STAPP_OP__JZ: case STAPP_OP__JNZ: case STAPP_OP__CALL:
      return 3;
    case STAPP_OP__PUSH32:
      return 5;
    case STAPP_OP__HALT: case STAPP_OP__DUP: case STAPP_OP__DROP: case STAPP_OP__SWAP:
    case STAPP_OP__RET:
      return 1;
    default:
      if(op >= STAPP_OP__ADD && op <= STAPP_OP__ABS)
        return 1;
      return -1;
  }
}

#endif // _STAPP_PRIVATE_H
