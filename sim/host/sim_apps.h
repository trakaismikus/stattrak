/***************************************************************************************************
 * @file    : sim_apps.h
 * @brief   : Install an app package in the simulator, like the web updater does (the window and
 *            web simulators, and the headless commands "simapp" / "simopen").
 *
 * The simulator opens "Apps: SYNC" (as the Settings row does) and uses the Bluetooth app commands
 * of docs/APPS.md section 6: BEGIN and ASK (the device asks "INSTALL <NAME>?"), STATUS until the
 * answer, then DATA and END. The person answers with the buttons (hold button 2 = yes), as on a device; or,
 * with `open`, the front end holds button 2 itself (SimApps_wantsYes) and the app opens after the
 * install. The firmware code of the install is the real code; only the phone is simulated.
 **************************************************************************************************/

#ifndef _SIM_APPS_H
#define _SIM_APPS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sim_apps_state_e {
  SIM_APPS__IDLE = 0,
  SIM_APPS__OPENING,      // Opens "Apps: SYNC" (waits for the end of the start screen)
  SIM_APPS__SENDING,      // After yes: DATA, END
  SIM_APPS__QUESTION,     // BEGIN, ASK: the device shows "INSTALL <NAME>?"
  SIM_APPS__INSTALLED,
  SIM_APPS__REFUSED,      // The person said no, or 30 s without an answer
  SIM_APPS__FAILED,       // See SimApps_getText()
} sim_apps_state_t;

// Starts an install. open: answer yes and open the app after the install (quick test).
// false: an install runs already, or the package has a bad size (then the text says why).
bool             SimApps_start(const uint8_t* package, uint32_t size, bool open);
// Call often (each loop of the simulator): it sends the commands and reads the answers.
void             SimApps_update(void);
sim_apps_state_t SimApps_getState(void);
// One line for the person (what happens now, or what went wrong)
const char*      SimApps_getText(void);
// true while the front end must hold button 2 for the person (an install with `open`)
bool             SimApps_wantsYes(void);

#endif // _SIM_APPS_H
