/***************************************************************************************************
 * @file    : host.h
 * @brief   : The "fake device" that runs the StatTrak firmware on a computer.
 *
 * The real firmware files (main/main.c, app.c, eeprom.c, battery.c, the views, the button logic)
 * are compiled without changes. This layer replaces only the hardware and the ESP-IDF SDK:
 *
 *   time      - simulated. It moves only when the firmware calls vTaskDelay().
 *   buttons   - two levels that a front end sets (keyboard or test script).
 *   display   - a 128x32 frame buffer that a front end can read.
 *   flash     - a small key-value table (NVS) that can be saved to a text file.
 *   battery   - a voltage that a front end sets.
 *   BLE / OTA - a stand-in that a front end drives ("fake transfer", "accept").
 *
 * Front ends: sim/headless (script driven, for tests and AI tools) and sim/sdl (a window).
 **************************************************************************************************/

#ifndef _HOST_H
#define _HOST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* > Defines **************************************************************************************/
#define HOST_OLED_WIDTH     (128)
#define HOST_OLED_HEIGHT    (32)
#define HOST_FRAME_SIZE     (HOST_OLED_WIDTH * HOST_OLED_HEIGHT / 8)

/* Simulated time moves in steps of this size. It is the period of the button driver. */
#define HOST_STEP_MS        (5)

#define HOST_NVS_MAX_ENTRIES  (64)
#define HOST_NVS_MAX_BLOBS    (128)
#define HOST_NVS_MAX_BLOB     (4096)          /* Bytes in one blob (the app packages are 4096 at most) */
#define HOST_NVS_BLOB_SPACE   (40 * 1024)     /* All blobs together: the NVS partition is 48 KB */

/* > Type Definitions *****************************************************************************/

/* Why a simulated power session stopped. */
typedef enum {
  HOST_END__QUIT = 0,     /* The front end stopped the session */
  HOST_END__POWER_OFF,    /* The firmware called Board_powerOff() */
  HOST_END__POWER_LOST,   /* No button is held and the firmware did not latch the power */
  HOST_END__RESET,        /* The firmware restarted the chip */
  HOST_END__ABORT,        /* ESP_ERROR_CHECK failed. On the device this is a crash and a reboot */
} host_end_t;

typedef struct {
  /* Called each time simulated time moves. The headless front end reads its commands here. */
  void (*idle)(void);
  /* Called when the firmware sends a frame to the display. */
  void (*frame)(const uint8_t* frame);
  /* Called when the saved device state (flash) changed. */
  void (*stateChanged)(void);
  /* Called when the session ends. If it returns, the process exits. */
  void (*end)(host_end_t reason, const char* detail);
} host_frontend_t;

typedef struct {
  const char* stateFile;      /* Text file with the flash content. NULL = keep in memory only */
  bool        log;            /* Print firmware log lines (ESP_LOGx) to stderr */
  bool        usbPower;       /* true: power does not depend on the buttons or the latch */
  bool        pendingVerify;  /* true: start as a new firmware that is not accepted yet */
  uint16_t    voltageMv;      /* Battery voltage at start */
  uint8_t     resetReason;    /* Why the chip started (main/device_log.h reasons); 0 = power on */
} host_config_t;

typedef struct {
  char     ns[16];
  char     key[16];
  uint32_t value;
} host_nvs_entry_t;

typedef struct {
  char     ns[16];
  char     key[16];
  uint16_t length;
  uint8_t* data;
} host_nvs_blob_t;

/* > Function Declarations ************************************************************************/

/* --- Session */
void        Host_init(const host_config_t* config, const host_frontend_t* frontend);
void        Host_run(void);                 /* Calls the firmware app_main(). Does not return */
void        Host_end(host_end_t reason, const char* detail)
#if defined(__GNUC__)
  __attribute__((noreturn))
#endif
  ;
const char* Host_endName(host_end_t reason);

/* --- Time */
uint64_t    Host_nowMs(void);
void        Host_sleepMs(uint32_t ms);

/* --- Saved device state (flash content + firmware update flags) */
bool        HostState_load(const char* path);
bool        HostState_save(const char* path);
void        HostState_markChanged(void);

/* --- Buttons (id 0 = button 1 / kill, id 1 = button 2 / death) */
void        HostButton_setLevel(uint8_t id, bool pressed);
bool        HostButton_getLevel(uint8_t id);
void        HostButton_tick(void);

/* --- Power and battery */
bool        HostBoard_isLatched(void);
void        HostBoard_setVoltage(uint16_t millivolts);
uint16_t    HostBoard_getVoltage(void);
void        HostBoard_setResetReason(uint8_t reason);

/* --- Display */
const uint8_t* HostOled_getFrame(void);       /* Last frame the firmware sent to the display */
uint32_t    HostOled_getFrameCount(void);
uint16_t    HostOled_getBrightness(void);
bool        HostOled_isOn(void);
bool        HostOled_guardsIntact(void);      /* false = the firmware wrote outside the frame buffer */
void        Host_notifyFrame(const uint8_t* frame);   /* Used by the display stand-in */

/* --- Buzzer */
uint32_t    HostBuzzer_getClickCount(void);
uint16_t    HostBuzzer_getLastFreq(void);

/* --- Flash (NVS) */
void        HostNvs_setFailWrites(bool fail);
void        HostNvs_setFailOpen(bool fail);
void        HostNvs_setNoFreePagesOnce(bool enable);
uint32_t    HostNvs_getWriteCount(void);
uint32_t    HostNvs_getDrawWriteCount(void);       // writes and key erases while the firmware draws
extern bool (*HostNvs_drawingHook)(void);          // the simulator program sets it (AppView_isDrawing)
uint32_t    HostNvs_getCommitCount(void);
uint32_t    HostNvs_getEraseCount(void);
uint32_t    HostNvs_getEntryCount(void);
const host_nvs_entry_t* HostNvs_getEntry(uint32_t idx);
bool        HostNvs_put(const char* ns, const char* key, uint32_t value);
uint32_t    HostNvs_getBlobCount(void);
const host_nvs_blob_t* HostNvs_getBlob(uint32_t idx);
bool        HostNvs_putBlob(const char* ns, const char* key, const uint8_t* data, uint32_t length);
void        HostNvs_clear(void);

/* --- Firmware update stand-in (replaces components/ble_ota/ota.c) */
/* Pending stage: 0 = firmware is accepted, 1 = new firmware, not started yet,
 * 2 = new firmware started but not accepted (the next start is a rollback). */
void        HostOta_setPendingStage(uint8_t stage);
uint8_t     HostOta_getPendingStage(void);
void        HostOta_boot(void);               /* Does what the bootloader does at each start */
bool        HostOta_wasRolledBack(void);      /* true: this start is a rollback to the old firmware */
bool        HostOta_isPendingVerify(void);
bool        HostOta_isActive(void);           /* true between Ota_init() and Ota_deinit() */
const char* HostOta_getName(void);            /* The Bluetooth name of the last Ota_init() */
uint32_t    HostOta_getInitCount(void);
uint32_t    HostOta_getDeinitCount(void);
uint32_t    HostOta_getDoubleInitCount(void); /* Ota_init() calls while already active */
uint8_t     HostOta_getState(void);
void        HostOta_setState(uint8_t state, uint8_t progress);
bool        HostOta_startTransfer(uint32_t durationMs);  /* A phone sends new firmware */
bool        HostOta_accept(void);                        /* A phone sends "accept firmware" */
bool        HostOta_isUpdateAllowed(void);               /* false: the firmware refuses new firmware */

/* A phone sends a user command (see main/ble_commands.h). false: Bluetooth is off, the number
 * is not a user command number, or too many commands wait. */
bool        HostOta_sendUserCommand(uint16_t cmdId, const uint8_t* data, uint8_t len);
/* The oldest answer that the firmware sent. `data` must have place for 13 bytes. */
bool        HostOta_takeUserResponse(uint16_t* cmdId, uint8_t* status, uint8_t* data, uint8_t* len);

#ifdef __cplusplus
}
#endif

#endif // _HOST_H
