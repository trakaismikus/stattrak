/***************************************************************************************************
 * @file    : gate.h
 * @brief   : The messages of the GATE Public API (TITAN II Bluetooth, ASTER II Bluetooth).
 *
 * Source: https://help.gatee.eu/space/GHC/4006150145 ("Specifications for GATE Public API").
 * No radio code here: components/gate_ble/ sends and receives the bytes. This file only builds
 * and reads them, so the unit tests can check it with the examples of the GATE document.
 *
 * Command frame (characteristic "Commands", 20 bytes at most):
 *   [0] sender / receiver: 0xEF = from us to the ETU, 0xFE = from the ETU to us
 *   [1] length of the whole frame
 *   [2] command, then its parameters
 *   [n] CRC-8 of all bytes before it (CRC-8/SMBUS: polynomial 0x07, start 0)
 **************************************************************************************************/

#ifndef _GATE_H
#define _GATE_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define GATE_FRAME_MAX        20
#define GATE_ID__TO_ETU       0xEF
#define GATE_ID__FROM_ETU     0xFE

#define GATE_CMD__PIN         0xB0    // PIN + serial number (12 ASCII). Necessary in 30 s after the connection.
#define GATE_CMD__FW_VERSION  0x56

#define GATE_PIN_LEN          6
#define GATE_SERIAL_LEN       6
#define GATE_NAME_MAX         20      // The Bluetooth name, for example "TTN2B BT RD5ZEI"

#define GATE_INFO_LEN         8       // The bytes of INFO_0_API that we use (of 20)
#define GATE_VOLTAGE_UNKNOWN  0xFFFF  // The ETU did not measure (no battery type set in GCS)

// INFO_0_API byte 1
#define GATE_FLAG__MAG_SENSOR   0x01  // The mag sensor is on
#define GATE_FLAG__NO_MAG       0x02  // No magazine
#define GATE_FLAG__BOLT_SENSOR  0x04  // The bolt catch sensor is on
#define GATE_FLAG__BOLT_OPEN    0x08  // The bolt catch is released
#define GATE_FLAG__BB_RESET     0x80  // The BB counter of the ETU was reset (a reload on the ETU side)

/* > Type Definitions *****************************************************************************/
typedef enum gate_selector_e {
  GATE_SELECTOR__UNDEFINED = 0,
  GATE_SELECTOR__SAFE,
  GATE_SELECTOR__SEMI,
  GATE_SELECTOR__AUTO,
} gate_selector_t;

typedef enum gate_answer_e {
  GATE_ANSWER__BAD = 0,   // Not a correct frame from the ETU
  GATE_ANSWER__OK,        // "OK"
  GATE_ANSWER__ERROR,     // "ER"
  GATE_ANSWER__DATA,      // An other answer (for example the firmware version)
} gate_answer_t;

// INFO_0_API (the state of the ETU)
typedef struct gate_info_s {
  bool     fireLocked;    // The ETU does not fire
  uint8_t  flags;         // GATE_FLAG__*
  uint8_t  selector;      // gate_selector_t
  uint8_t  trigger;       // 0 released, 1 pressed, 2 second stage
  uint16_t fired;         // BBs fired since the last notification
  uint16_t voltageMv;     // GATE_VOLTAGE_UNKNOWN = not measured
} gate_info_t;

/* > Function Declarations ************************************************************************/

// CRC-8/SMBUS of `len` bytes
uint8_t Gate_crc8(const uint8_t* data, uint8_t len);

// A command frame into `out` (GATE_FRAME_MAX bytes). Returns its length, 0 if the parameters
// are too long.
uint8_t Gate_buildCommand(uint8_t* out, uint8_t cmd, const uint8_t* params, uint8_t paramLen);

// The PIN command: 6 PIN characters + 6 serial characters. Returns the length (16) or 0.
uint8_t Gate_buildPin(uint8_t* out, const char* pin, const char* serial);

// Reads an answer frame of the ETU. `cmd` gets the command that it answers (can be NULL).
gate_answer_t Gate_parseAnswer(const uint8_t* frame, uint8_t len, uint8_t* cmd);

// Reads INFO_0_API (at least GATE_INFO_LEN bytes). false = too short.
bool Gate_parseInfo(const uint8_t* data, uint8_t len, gate_info_t* info);

// The Bluetooth name of an ETU: "TTN2B BT RD5ZEI" = model "TTN2B", serial "RD5ZEI".
// `serial` gets GATE_SERIAL_LEN + 1 bytes. false = not the name of a GATE ETU.
bool Gate_parseName(const char* name, char* serial);

// A character that can be in a PIN or a serial number (printable, no space)
bool Gate_isPinChar(char c);

#endif // _GATE_H
