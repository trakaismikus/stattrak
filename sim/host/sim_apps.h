/***************************************************************************************************
 * @file    : sim_apps.h
 * @brief   : Install an app package in the simulator, like the web updater does (the window and
 *            web simulators, and the headless command "simapp").
 *
 * The simulator opens Settings > "Apps: SYNC" and sends the package with the Bluetooth app
 * commands of docs/APPS.md section 6 (BEGIN, DATA, END, STATUS). The device then asks
 * "INSTALL <NAME>?" and the person answers with the buttons (hold button 2 = yes), as on a device.
 * The firmware code of the install is the real code; only the phone is simulated.
 **************************************************************************************************/

#ifndef _SIM_APPS_H
#define _SIM_APPS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sim_apps_state_e {
  SIM_APPS__IDLE = 0,
  SIM_APPS__OPENING,      // Opens "Apps: SYNC"
  SIM_APPS__SENDING,      // BEGIN, DATA, END
  SIM_APPS__QUESTION,     // The device shows "INSTALL <NAME>?"
  SIM_APPS__INSTALLED,
  SIM_APPS__REFUSED,      // The person said no, or 30 s without an answer
  SIM_APPS__FAILED,       // See SimApps_getText()
} sim_apps_state_t;

// Starts an install. false: an install runs already, or the package is too large.
bool             SimApps_start(const uint8_t* package, uint32_t size);
// Call often (each loop of the simulator): it sends the commands and reads the answers.
void             SimApps_update(void);
sim_apps_state_t SimApps_getState(void);
// One line for the person (what happens now, or what went wrong)
const char*      SimApps_getText(void);

#endif // _SIM_APPS_H
