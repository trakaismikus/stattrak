/***************************************************************************************************
 * @file    : host_ota.c
 * @brief   : Stand-in for components/ble_ota/ota.c (firmware update over Bluetooth).
 *
 * There is no Bluetooth and no flash on a computer. This file keeps the same API and the same
 * states, and a front end plays the role of the phone:
 *
 *   HostOta_startTransfer()  - "the phone sends new firmware"
 *   HostOta_accept()         - "the phone accepts the new firmware"
 *
 * IMPORTANT: this stand-in can NOT show that the real Bluetooth update works in your firmware.
 * Only a real device can show that.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "host.h"

#include <string.h>

#include "ota.h"

/* > Defines **************************************************************************************/
#define USER_QUEUE_LENGTH     (4)   /* The same as OTA_USER_QUEUE_LENGTH on the device */
#define USER_RESPONSE_LENGTH  (8)

/* > Type Definitions *****************************************************************************/
typedef struct {
  uint16_t cmdId;
  uint8_t  status;
  uint8_t  data[OTA_USER_RESPONSE_SIZE];
  uint8_t  len;
} user_response_t;

/* > Local Variables ******************************************************************************/
static bool     s_active = false;
static uint8_t  s_pendingStage = 0;
static bool     s_rolledBack = false;

static uint8_t  s_state = OTA_STATE__IDLE;
static uint8_t  s_progress = 0;

static bool     s_transfer = false;
static uint64_t s_transferStart = 0;
static uint32_t s_transferDuration = 0;

static uint32_t s_initCount = 0;
static uint32_t s_deinitCount = 0;
static uint32_t s_doubleInitCount = 0;

static bool            s_updateAllowed = true;
static ota_user_cmd_t  s_userCmds[USER_QUEUE_LENGTH];
static uint8_t         s_userCmdCount = 0;
static user_response_t s_userResponses[USER_RESPONSE_LENGTH];
static uint8_t         s_userResponseCount = 0;

/* > Front end API ********************************************************************************/

void HostOta_setPendingStage(uint8_t stage)   { s_pendingStage = (stage <= 2) ? stage : 0; }
uint8_t HostOta_getPendingStage(void)         { return s_pendingStage; }
bool HostOta_wasRolledBack(void)              { return s_rolledBack; }
bool HostOta_isPendingVerify(void)            { return s_pendingStage != 0; }

void HostOta_boot(void) {
  s_rolledBack = false;

  if(s_pendingStage == 2) {
    /* The new firmware started one time and nobody accepted it: the bootloader goes back. */
    s_pendingStage = 0;
    s_rolledBack = true;
    HostState_markChanged();
  } else if(s_pendingStage == 1) {
    s_pendingStage = 2;
    HostState_markChanged();
  }
}
bool HostOta_isActive(void)                   { return s_active; }
uint32_t HostOta_getInitCount(void)           { return s_initCount; }
uint32_t HostOta_getDeinitCount(void)         { return s_deinitCount; }
uint32_t HostOta_getDoubleInitCount(void)     { return s_doubleInitCount; }

void HostOta_setState(uint8_t state, uint8_t progress) {
  s_transfer = false;
  s_state = state;
  s_progress = progress;
}

bool HostOta_isUpdateAllowed(void)            { return s_updateAllowed; }

bool HostOta_sendUserCommand(uint16_t cmdId, const uint8_t* data, uint8_t len) {
  /* The same rules as the device: Bluetooth must be on, the number must be a user command
   * number, and the queue has a few places only. */
  if(!s_active || cmdId < OTA_USER_CMD_FIRST || s_userCmdCount >= USER_QUEUE_LENGTH)
    return false;

  ota_user_cmd_t* cmd = &s_userCmds[s_userCmdCount++];
  memset(cmd, 0, sizeof(*cmd));
  cmd->cmdId = cmdId;
  if(len > OTA_USER_REQUEST_SIZE)
    len = OTA_USER_REQUEST_SIZE;
  if(data != NULL && len > 0)
    memcpy(cmd->data, data, len);
  return true;
}

bool HostOta_takeUserResponse(uint16_t* cmdId, uint8_t* status, uint8_t* data, uint8_t* len) {
  if(s_userResponseCount == 0)
    return false;

  const user_response_t response = s_userResponses[0];
  s_userResponseCount--;
  memmove(&s_userResponses[0], &s_userResponses[1], s_userResponseCount * sizeof(s_userResponses[0]));

  *cmdId = response.cmdId;
  *status = response.status;
  *len = response.len;
  memcpy(data, response.data, response.len);
  return true;
}

bool HostOta_startTransfer(uint32_t durationMs) {
  /* A phone can connect only while the firmware keeps Bluetooth on, and the firmware can
   * refuse new firmware (Ota_setUpdateAllowed). */
  if(!s_active || !s_updateAllowed)
    return false;

  s_transfer = true;
  s_transferStart = Host_nowMs();
  s_transferDuration = (durationMs > 0) ? durationMs : 1;
  s_state = OTA_STATE__STARTED;
  s_progress = 0;
  return true;
}

bool HostOta_accept(void) {
  if(!s_active)
    return false;

  /* Same as the device: the state changes to ACCEPTED only when a firmware was accepted. */
  const bool accepted = Ota_acceptPending(true);
  if(accepted) {
    s_state = OTA_STATE__ACCEPTED;
    s_progress = 100;
  }
  return accepted;
}

uint8_t HostOta_getState(void) {
  return s_state;
}

/* > components/ble_ota/ota.h *********************************************************************/

int Ota_init(void) {
  s_initCount++;
  if(s_active)
    s_doubleInitCount++;

  s_active = true;
  s_transfer = false;
  s_state = OTA_STATE__IDLE;
  s_progress = 0;
  s_updateAllowed = true;
  s_userCmdCount = 0;
  s_userResponseCount = 0;
  return 0;
}

void Ota_deinit(void) {
  s_deinitCount++;
  s_active = false;
  s_transfer = false;
  s_userCmdCount = 0;
}

void Ota_setUpdateAllowed(bool allowed) {
  s_updateAllowed = allowed;
}

bool Ota_getUserCommand(ota_user_cmd_t* cmd) {
  if(!s_active || cmd == NULL || s_userCmdCount == 0)
    return false;

  *cmd = s_userCmds[0];
  s_userCmdCount--;
  memmove(&s_userCmds[0], &s_userCmds[1], s_userCmdCount * sizeof(s_userCmds[0]));
  return true;
}

bool Ota_sendUserResponse(uint16_t cmdId, uint8_t status, const uint8_t* data, uint8_t len) {
  if(!s_active || len > OTA_USER_RESPONSE_SIZE || (data == NULL && len > 0))
    return false;

  if(s_userResponseCount >= USER_RESPONSE_LENGTH)
    return false; /* Nobody reads the answers */

  user_response_t* response = &s_userResponses[s_userResponseCount++];
  memset(response, 0, sizeof(*response));
  response->cmdId = cmdId;
  response->status = status;
  response->len = len;
  if(len > 0)
    memcpy(response->data, data, len);
  return true;
}

int Ota_start(void) {
  return 0;
}

int Ota_write(const uint8_t* data, uint16_t len) {
  (void)data;
  (void)len;
  return 0;
}

int Ota_end(void) {
  return 0;
}

void Ota_abort(void) {
}

bool Ota_isPendingVerify(void) {
  return s_pendingStage != 0;
}

bool Ota_acceptPending(bool accept) {
  if(s_pendingStage == 0)
    return false;

  s_pendingStage = 0;
  HostState_markChanged();

  if(!accept) {
    /* The device marks the firmware as bad and restarts into the previous firmware. */
    Host_end(HOST_END__RESET, "firmware rejected, rollback");
  }

  return true;
}

uint8_t Ota_getState(void) {
  /* The device returns IDLE while the update service is off. */
  if(!s_active)
    return OTA_STATE__IDLE;

  if(s_transfer) {
    const uint64_t elapsed = Host_nowMs() - s_transferStart;

    if(elapsed >= s_transferDuration) {
      s_transfer = false;
      s_state = OTA_STATE__FINISHED;
      s_progress = 100;

      /* The new firmware starts in the "pending verify" state after the restart. */
      s_pendingStage = 1;
      HostState_markChanged();
    } else {
      s_state = OTA_STATE__WRITING;
      s_progress = (uint8_t)((elapsed * 100) / s_transferDuration);
    }
  }

  return s_state;
}

uint8_t Ota_getProgress(void) {
  return s_progress;
}
