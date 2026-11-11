#ifndef _OTA_H
#define _OTA_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/

// Bluetooth commands for your own features ("user commands").
// The command numbers below OTA_USER_CMD_FIRST are for the firmware update. Do not use them.
#define OTA_USER_CMD_FIRST        (0x0100)
#define OTA_USER_REQUEST_SIZE     (16)  // Data bytes that the phone sends with a command
#define OTA_USER_RESPONSE_SIZE    (13)  // Data bytes that the device can send back

/* > Enumerations **********************************************************************************/
enum ota_stete_e {
  OTA_STATE__IDLE = 0,
  OTA_STATE__STARTED,
  OTA_STATE__WRITING,
  OTA_STATE__FINISHED,
  OTA_STATE__ACCEPTED,
  OTA_STATE__ERROR,

  OTA_STATE_COUNT
};


/* > Type Definitions ******************************************************************************/

// A user command that came over Bluetooth
typedef struct {
  uint16_t cmdId;
  uint8_t  data[OTA_USER_REQUEST_SIZE];
} ota_user_cmd_t;


#ifdef __cplusplus
extern "C" {
#endif

/* > Function Declarations *************************************************************************/

int Ota_init(void);
void Ota_deinit(void);
int Ota_start(void);
int Ota_write(const uint8_t* data, uint16_t len);
int Ota_end(void);
void Ota_abort(void);

// True while firmware is being received or the device is about to restart
static inline bool Ota_isBusy(uint8_t state) {
  return (state == OTA_STATE__STARTED || state == OTA_STATE__WRITING || state == OTA_STATE__FINISHED);
}

bool Ota_isPendingVerify(void);
bool Ota_acceptPending(bool accept);

uint8_t Ota_getState(void);
uint8_t Ota_getProgress(void);

// --- Bluetooth for your own features (see docs/BLUETOOTH.md)

// false: the device refuses new firmware, but Bluetooth and the user commands stay on.
// Ota_init() always sets this back to true. Never call it in the update view or the accept view.
void Ota_setUpdateAllowed(bool allowed);

// Call from the main loop. Returns true and fills `cmd` when a user command came.
bool Ota_getUserCommand(ota_user_cmd_t* cmd);

// Sends the answer to a user command. `status` 0 = OK. `len` is OTA_USER_RESPONSE_SIZE at most.
bool Ota_sendUserResponse(uint16_t cmdId, uint8_t status, const uint8_t* data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif // _OTA_H