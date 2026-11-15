/***************************************************************************************************
 * @file    : gate_ble.h
 * @brief   : Bluetooth to a GATE ETU (TITAN II Bluetooth). StatTrak is the "phone" side here
 *            (the central): it searches, connects and reads. It does not advertise.
 *
 * Only for the TITAN app (main/views/titan_view.c, main/titan.c). This component is separate
 * from the firmware update (components/ble_ota, components/app_ble), and it never runs at the
 * same time: the TITAN view starts it, and stops it in its deinit. The update view, the accept
 * view and "Apps: SYNC" start the update service; they are never opened directly from the TITAN
 * view (docs/BLUETOOTH.md, "Never go from your Bluetooth view directly to the update view").
 *
 * All functions are for the main loop. The radio task puts events into a queue;
 * GateBle_getEvent() takes them. The simulator has a stand-in: sim/host/host_gate.c.
 **************************************************************************************************/

#ifndef _GATE_BLE_H
#define _GATE_BLE_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/
#define GATE_BLE_DATA_MAX   20
#define GATE_BLE_NAME_MAX   20

/* > Type Definitions *****************************************************************************/
typedef enum gate_ble_event_type_e {
  GATE_BLE_EV__NONE = 0,
  GATE_BLE_EV__SCAN_RESULT,     // name, rssi: a GATE ETU is near (GateBle_scan)
  GATE_BLE_EV__SCAN_DONE,       // The search time has ended
  GATE_BLE_EV__CONNECTED,       // Connected, and the notifications are on (GateBle_connect)
  GATE_BLE_EV__CONNECT_FAILED,  // Not found in the time, or the connection did not work
  GATE_BLE_EV__DISCONNECTED,    // The connection is lost (after CONNECTED)
  GATE_BLE_EV__INFO,            // data: INFO_0_API, a notification
  GATE_BLE_EV__INFO_READ,       // data: INFO_0_API, the answer of GateBle_readInfo()
  GATE_BLE_EV__ANSWER,          // data: a frame on "Commands" (the answer to GateBle_send)
} gate_ble_event_type_t;

typedef struct gate_ble_event_s {
  uint8_t type;                         // gate_ble_event_type_t
  int8_t  rssi;                         // SCAN_RESULT: signal strength in dBm
  uint8_t len;                          // Bytes in data
  uint8_t data[GATE_BLE_DATA_MAX];
  char    name[GATE_BLE_NAME_MAX + 1];  // SCAN_RESULT: the Bluetooth name of the ETU
} gate_ble_event_t;

/* > Function Declarations ************************************************************************/

// The radio on (the central role only). false: not possible now (no memory, already on).
// Start it with the soft radio start of main/views/bt_start.h.
bool GateBle_start(void);

// The radio off. Disconnects first. No events after this.
void GateBle_stop(void);

bool GateBle_isOn(void);

// Search for GATE ETUs for `ms`. SCAN_RESULT for each one (one time each), then SCAN_DONE.
// false: the radio is not ready yet, or it is busy (connected or connecting).
bool GateBle_scan(uint32_t ms);

// Search for the ETU with this serial number (6 characters) for `ms`, and connect to it.
// CONNECTED or CONNECT_FAILED comes later. false: not ready yet, or busy.
bool GateBle_connect(const char* serial, uint32_t ms);

// Stop a search or a connection. No DISCONNECTED event for it.
void GateBle_disconnect(void);

bool GateBle_isConnected(void);

// Write a frame to the "Commands" characteristic. The answer comes as GATE_BLE_EV__ANSWER.
bool GateBle_send(const uint8_t* frame, uint8_t len);

// Read INFO_0_API. The value comes as GATE_BLE_EV__INFO_READ.
bool GateBle_readInfo(void);

// The next event. false: no event.
bool GateBle_getEvent(gate_ble_event_t* event);

#endif // _GATE_BLE_H
