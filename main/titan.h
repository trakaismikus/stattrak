/***************************************************************************************************
 * @file    : titan.h
 * @brief   : The TITAN app without the screens: the paired ETU, its settings, the connection,
 *            the shot count and the alerts. qa/titan-app/decisions.md is the specification.
 *
 * The screens are in main/views/titan_view.c and titan_setup_view.c. The radio is
 * components/gate_ble/ (the simulator: sim/host/host_gate.c).
 *
 * What StatTrak sends to the ETU: only the PIN and reads. No settings, no fire commands.
 *
 * Flash: NVS namespace "titan", blob "cfg" (not in ENTRY_LIST: the serial number and the PIN are
 * text). It is written at pairing, at a settings change and when the app closes, never for each
 * shot.
 **************************************************************************************************/

#ifndef _TITAN_H
#define _TITAN_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "gate.h"

/* > Defines **************************************************************************************/
#define TITAN_FOUND_MAX       6       // ETUs in the pick list
#define TITAN_MAG_STEPS       25      // MAG SIZE: 0 = OFF, 1 .. 25 = 10 .. 250 rounds
#define TITAN_SCAN_MS         10000   // The search for the pick list
#define TITAN_LINK_LOST_MS    5000    // LINK LOST after this time without a connection
#define TITAN_LOW_BATT_MS     3000    // The voltage stays under the limit this long
#define TITAN_PIN_ANSWER_MS   5000    // The ETU answers the PIN in this time, or we connect again
#define TITAN_INFO_READ_MS    2000    // Read INFO_0_API this often (the voltage changes slowly)

#define TITAN_PIN_CHARS       "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

/* > Type Definitions *****************************************************************************/
// GUN BATT. A stored value is the position in this list: add new types at the end only.
typedef enum titan_batt_e {
  TITAN_BATT__2S_LIPO = 0,
  TITAN_BATT__3S_LIPO,
  TITAN_BATT__2S_LIFE,
  TITAN_BATT__3S_LIFE,
  TITAN_BATT__NIMH_84,
  TITAN_BATT__NIMH_96,
  TITAN_BATT__OFF,
  TITAN_BATT_COUNT,
} titan_batt_t;

// AMMO ALERT: OFF, 10 %, 20 %, 30 %
#define TITAN_AMMO_ALERT_COUNT  4

typedef enum titan_link_e {
  TITAN_LINK__OFF = 0,      // No connection wanted
  TITAN_LINK__SEARCHING,    // Searching for the paired ETU, or connecting
  TITAN_LINK__PIN,          // Connected, waiting for the answer to the PIN
  TITAN_LINK__READY,        // The ETU took the PIN: data comes
  TITAN_LINK__WRONG_PIN,    // The ETU said "ER" to the PIN. No new try until Titan_linkStart().
} titan_link_t;

// Alerts, most important first (Titan_getAlert gives the first pending one)
typedef enum titan_alert_e {
  TITAN_ALERT__LINK_LOST   = 0x01,
  TITAN_ALERT__LOW_BATT    = 0x02,
  TITAN_ALERT__FIRE_LOCKED = 0x04,
  TITAN_ALERT__MAG_EMPTY   = 0x08,
  TITAN_ALERT__LOW_AMMO    = 0x10,
} titan_alert_t;

typedef struct titan_found_s {
  char   serial[GATE_SERIAL_LEN + 1];
  int8_t rssi;
} titan_found_t;

/* > Function Declarations ************************************************************************/

// --- Settings and pairing (flash)

void        Titan_load(void);                   // From flash (the view calls it at open)
bool        Titan_isPaired(void);
const char* Titan_getSerial(void);              // "" when not paired
const char* Titan_getPin(void);
void        Titan_setPaired(const char* serial, const char* pin);   // Saves it
void        Titan_forget(void);                 // Saves it

uint8_t     Titan_getMagSize(void);             // 0 = OFF, 1 .. 25 = x 10 rounds
void        Titan_setMagSize(uint8_t steps);    // The mag is full again. Saves it.
uint16_t    Titan_getCapacity(void);            // Rounds of a full mag, 0 = OFF
uint8_t     Titan_getAmmoAlert(void);           // 0 OFF, 1 10 %, 2 20 %, 3 30 %
void        Titan_setAmmoAlert(uint8_t value);
uint8_t     Titan_getBattery(void);             // titan_batt_t
void        Titan_setBattery(uint8_t type);
const char* Titan_batteryName(uint8_t type);
uint16_t    Titan_batteryLimitMv(uint8_t type); // The LOW BATT voltage, 0 = no alert

// --- The app is open / closed (session time, saved count)

void        Titan_open(uint32_t now);
void        Titan_close(uint32_t now);           // Saves the count

// --- Search for the pick list

bool        Titan_scanStart(uint32_t now);
void        Titan_scanStop(void);
bool        Titan_isScanning(void);
uint8_t     Titan_getFoundCount(void);
const titan_found_t* Titan_getFound(uint8_t index);   // The strongest signal first

// --- Connection to one ETU

void        Titan_linkStart(const char* serial, const char* pin, uint32_t now);
void        Titan_linkStop(void);
uint8_t     Titan_getLink(void);                 // titan_link_t
bool        Titan_wasReady(void);                // The connection was READY once since Titan_linkStart

// Each loop while the app is open: radio events, connection steps, alerts
void        Titan_update(uint32_t now);

// --- Count

int32_t     Titan_getRounds(void);               // MAG SIZE set: rounds left. OFF: shots of the session.
bool        Titan_isUncertain(void);             // Shots can be missing ("~" on the display)
void        Titan_reload(void);                  // The mag is full again
void        Titan_newSession(uint32_t now);      // Shots, reloads and time 0; the mag is full
uint32_t    Titan_getSessionShots(void);
uint16_t    Titan_getReloads(void);
uint32_t    Titan_getSessionMs(uint32_t now);
bool        Titan_getInfo(gate_info_t* info);    // The last state of the ETU. false: none yet.

// --- Alerts

uint8_t     Titan_getActiveAlerts(void);         // titan_alert_t bits: the problems now
uint8_t     Titan_getAlert(void);                // The pending alert to show (one bit), 0 = none
void        Titan_dismissAlert(void);            // A button closed the alert

#endif // _TITAN_H
