/***************************************************************************************************
 * @file    : battery.c
 * @brief   : Battery driver source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "battery.h"
#include "board.h"


/* > Defines **************************************************************************************/

#define LIPO_CRITICAL_VOLTAGE (3300)
#define LIPO_MIN_VOLTAGE      (3600)
#define LIPO_MAX_VOLTAGE      (4200)

#define NIMH_CRITICAL_VOLTAGE (900)
#define NIMH_MIN_VOLTAGE      (1000)
#define NIMH_MAX_VOLTAGE      (1400)

/* > Type Definitions *****************************************************************************/

typedef struct {
  uint8_t type;
  uint8_t typeDetected;
  uint16_t voltage;
} battery_t;

typedef struct {
  uint16_t critical;
  uint16_t min;
  uint16_t max;
} battery_specs_t;

/* > Constants ************************************************************************************/

const uint16_t kLipoBatteryVoltages[] = {
  // 3300, 6600, 9900, 14000,
  (4300 * 1),  (4300 * 2),  (4300 * 3),
};

const battery_specs_t kBatterySpecs[] = {
  { 0, 0, 0 }, // Unknown
  { 0, 0, 0 }, // USB
  { LIPO_CRITICAL_VOLTAGE, LIPO_MIN_VOLTAGE, LIPO_MAX_VOLTAGE }, // RCR123A (similar to LiPo 1S)
  { LIPO_CRITICAL_VOLTAGE, LIPO_MIN_VOLTAGE, LIPO_MAX_VOLTAGE }, // LiPo 1S
  { (2 * LIPO_CRITICAL_VOLTAGE), (2 * LIPO_MIN_VOLTAGE), (2 * LIPO_MAX_VOLTAGE) }, // LiPo 2S
  { (3 * LIPO_CRITICAL_VOLTAGE), (3 * LIPO_MIN_VOLTAGE), (3 * LIPO_MAX_VOLTAGE) }, // LiPo 3S
  { (3 * NIMH_CRITICAL_VOLTAGE), (3 * NIMH_MIN_VOLTAGE), (3 * NIMH_MAX_VOLTAGE) }, // NiMH 3S
  { (8 * NIMH_CRITICAL_VOLTAGE), (8 * NIMH_MIN_VOLTAGE), (8 * NIMH_MAX_VOLTAGE) }, // NiMH 8S
};

/* > Local Variables ******************************************************************************/

battery_t s_battery;

/* > Function Definitions *************************************************************************/

void Battery_init(void) {
  s_battery.typeDetected = Battery_detect();
}


void Battery_read(void) {
  s_battery.voltage = Board_readVoltage();
}


// Auto Detection only for LiPo batteries!
uint8_t Battery_detect(void) {
  uint8_t i;
  uint16_t voltage = Board_readVoltage();
  
  for(i=0; i<3; i++) {
    if(voltage < kLipoBatteryVoltages[i])
      break;
  }

  return BATTERY_TYPE__LIPO_1S + i;
}

uint8_t Battery_readLevel(void) {
  if(s_battery.type == BATTERY_TYPE__UNKNOWN || s_battery.type == BATTERY_TYPE__USB) {
    return 100;
  } else {
    // uint8_t type = (s_battery.type == BATTERY_TYPE__AUTO) ? s_battery.typeDetected : s_battery.type;
    battery_specs_t specs = kBatterySpecs[s_battery.type];

    int32_t level = (100 * (Board_readVoltage() - specs.min)) / (specs.max - specs.min);

    // Limit
    if(level < 0) level = 0;
    else if(level > 100) level = 100;

    return level;
  }
}

bool Battery_isCritical(void) {
  if(s_battery.type == BATTERY_TYPE__UNKNOWN) {
    return false;
  } else {
    battery_specs_t specs = kBatterySpecs[s_battery.type];

    return (Board_readVoltage() <= specs.critical);
  }
}

uint16_t Battery_readVoltage(void) {
  return s_battery.voltage;//Board_readVoltage();
}

uint8_t Battery_getType(void) {
  return s_battery.type;
}

void Battery_setType(uint8_t type) {
  s_battery.type = type;
}

uint8_t Battery_getTypeUsed(void) {
  // return (s_battery.type == BATTERY_TYPE__AUTO) ? s_battery.typeDetected : s_battery.type;
  return s_battery.type;
}