/***************************************************************************************************
 * @file    : gate.c
 * @brief   : The messages of the GATE Public API (see gate.h).
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "gate.h"

#include <string.h>

/* > Defines **************************************************************************************/
#define NAME_SUFFIX_POS   5        // "TTN2B BT RD5ZEI": " BT " at 5, the serial at 9
#define NAME_LEN          (NAME_SUFFIX_POS + 4 + GATE_SERIAL_LEN)

/* > Function Definitions *************************************************************************/

uint8_t Gate_crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0;
  for(uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for(uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

uint8_t Gate_buildCommand(uint8_t* out, uint8_t cmd, const uint8_t* params, uint8_t paramLen) {
  const uint8_t len = (uint8_t)(4 + paramLen);
  if(out == NULL || paramLen > GATE_FRAME_MAX - 4 || (paramLen > 0 && params == NULL))
    return 0;

  out[0] = GATE_ID__TO_ETU;
  out[1] = len;
  out[2] = cmd;
  if(paramLen > 0)
    memcpy(&out[3], params, paramLen);
  out[len - 1] = Gate_crc8(out, (uint8_t)(len - 1));
  return len;
}

uint8_t Gate_buildPin(uint8_t* out, const char* pin, const char* serial) {
  uint8_t params[GATE_PIN_LEN + GATE_SERIAL_LEN];
  if(pin == NULL || serial == NULL || strlen(pin) != GATE_PIN_LEN || strlen(serial) != GATE_SERIAL_LEN)
    return 0;
  memcpy(&params[0], pin, GATE_PIN_LEN);
  memcpy(&params[GATE_PIN_LEN], serial, GATE_SERIAL_LEN);
  return Gate_buildCommand(out, GATE_CMD__PIN, params, sizeof(params));
}

gate_answer_t Gate_parseAnswer(const uint8_t* frame, uint8_t len, uint8_t* cmd) {
  if(frame == NULL || len < 4 || len > GATE_FRAME_MAX)
    return GATE_ANSWER__BAD;
  const uint8_t frameLen = frame[1];
  if(frame[0] != GATE_ID__FROM_ETU || frameLen < 4 || frameLen > len)
    return GATE_ANSWER__BAD;
  if(Gate_crc8(frame, (uint8_t)(frameLen - 1)) != frame[frameLen - 1])
    return GATE_ANSWER__BAD;

  if(cmd != NULL)
    *cmd = frame[2];
  if(frameLen == 6 && frame[3] == 'O' && frame[4] == 'K')
    return GATE_ANSWER__OK;
  if(frameLen == 6 && frame[3] == 'E' && frame[4] == 'R')
    return GATE_ANSWER__ERROR;
  return GATE_ANSWER__DATA;
}

bool Gate_parseInfo(const uint8_t* data, uint8_t len, gate_info_t* info) {
  if(data == NULL || info == NULL || len < GATE_INFO_LEN)
    return false;
  info->fireLocked = (data[0] != 0);
  info->flags = data[1];
  info->selector = (data[2] <= GATE_SELECTOR__AUTO) ? data[2] : GATE_SELECTOR__UNDEFINED;
  info->trigger = data[3];
  info->fired = (uint16_t)(data[4] | (data[5] << 8));
  info->voltageMv = (uint16_t)(data[6] | (data[7] << 8));
  return true;
}

bool Gate_isPinChar(char c) {
  return c > ' ' && c <= '~';
}

bool Gate_parseName(const char* name, char* serial) {
  if(name == NULL || serial == NULL || strlen(name) != NAME_LEN)
    return false;
  if(memcmp(&name[NAME_SUFFIX_POS], " BT ", 4) != 0)
    return false;
  for(uint8_t i = 0; i < NAME_SUFFIX_POS; i++) {
    if(!Gate_isPinChar(name[i]))
      return false;
  }
  for(uint8_t i = 0; i < GATE_SERIAL_LEN; i++) {
    const char c = name[NAME_SUFFIX_POS + 4 + i];
    if(!Gate_isPinChar(c))
      return false;
    serial[i] = c;
  }
  serial[GATE_SERIAL_LEN] = '\0';
  return true;
}
