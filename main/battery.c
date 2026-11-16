/***************************************************************************************************
 * @file    : battery.c
 * @brief   : Battery driver source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 *
 * The level (0..100 %) comes from the voltage of one cell and a discharge curve of the battery
 * chemistry (lithium: LiPo, Li-ion, RCR123A; NiMH). A lithium cell has a flat middle and a steep
 * start and end, so a straight line from min to max showed a fast drop at the start and at the end.
 *
 * The level is calm: the voltage is averaged over about 26 s (the load of the radio, the buzzer and
 * the display moves it for a short time), and the shown level changes only when the curve is clearly
 * at an other value (down: 1.5 % under the middle of the shown value; up: 3 % over it, for example on a
 * charger or after a large load). The critical check uses the fast voltage, as before.
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "battery.h"
#include "board.h"
#include "time.h"

#include <assert.h>


/* > Defines **************************************************************************************/

#define LIPO_CRITICAL_VOLTAGE (3300)
#define NIMH_CRITICAL_VOLTAGE (900)

#define CURVE_POINTS          (11)    // 0 %, 10 %, ... 100 %

#define SAMPLE_MS             (100)   // One step of the average each 100 ms
#define AVERAGE_SHIFT         (8)     // Each step moves the average 1/256 of the way: about 26 s

// The shown level changes when the curve is this far from the middle of the shown value
// (in 0.1 %). Rounding alone changes at 5: the rest is a dead band against noise.
#define LEVEL_DOWN_TENTHS     (15)
#define LEVEL_UP_TENTHS       (30)

/* > Type Definitions *****************************************************************************/

typedef struct {
  uint8_t type;
  uint8_t typeDetected;
  uint16_t voltage;           // The last sample (fast): the critical check
  bool     averageStarted;
  int32_t  averageAcc;        // The average voltage << AVERAGE_SHIFT
  uint32_t lastSampleMs;
  int16_t  level;             // The shown level, -1 = none yet
} battery_t;

typedef struct {
  const uint16_t* curve;      // mV of one cell at 0 %, 10 %, ... 100 %; NULL = no battery
  uint8_t cells;
  uint16_t critical;          // mV of the whole battery
} battery_specs_t;

/* > Constants ************************************************************************************/

const uint16_t kLipoBatteryVoltages[] = {
  // 3300, 6600, 9900, 14000,
  (4300 * 1),  (4300 * 2),  (4300 * 3),
};

// One lithium cell (LiPo, Li-ion, rechargeable RCR123A) at a small load. The middle is flat:
// 3.77 V to 3.87 V is 30 % to 60 %. Under 3.68 V the voltage falls fast: only 10 % is left.
static const uint16_t kLithiumCurve[CURVE_POINTS] = {
  3500, 3680, 3740, 3770, 3790, 3820, 3870, 3920, 3980, 4060, 4200,
};

// One NiMH cell: flat from 1.16 V to 1.27 V (20 % to 80 %)
static const uint16_t kNimhCurve[CURVE_POINTS] = {
  1000, 1120, 1160, 1180, 1200, 1210, 1230, 1250, 1270, 1300, 1400,
};

const battery_specs_t kBatterySpecs[] = {
  { NULL, 0, 0 },                                     // Unknown
  { NULL, 0, 0 },                                     // USB
  { kLithiumCurve, 1, LIPO_CRITICAL_VOLTAGE },        // RCR123A (similar to LiPo 1S)
  { kLithiumCurve, 1, LIPO_CRITICAL_VOLTAGE },        // LiPo 1S
  { kLithiumCurve, 2, 2 * LIPO_CRITICAL_VOLTAGE },    // LiPo 2S
  { kLithiumCurve, 3, 3 * LIPO_CRITICAL_VOLTAGE },    // LiPo 3S
  { kNimhCurve,    3, 3 * NIMH_CRITICAL_VOLTAGE },    // NiMH 3S
  { kNimhCurve,    8, 8 * NIMH_CRITICAL_VOLTAGE },    // NiMH 8S
};

static_assert((sizeof(kBatterySpecs)/sizeof(kBatterySpecs[0])) == BATTERY_TYPE_COUNT, "All battery types should have specs");

/* > Local Variables ******************************************************************************/

static battery_t s_battery = { .level = -1 };

/* > Local Function Declarations ******************************************************************/

static bool     _hasBattery(void);
static uint16_t _averageVoltage(void);
static int32_t  _curveTenths(uint16_t millivolts);
static void     _updateLevel(void);

/* > Function Definitions *************************************************************************/

void Battery_init(void) {
  s_battery.typeDetected = Battery_detect();
}


void Battery_read(void) {
  const uint32_t now = Time_getMillis();
  s_battery.voltage = Board_readVoltage();

  if(!s_battery.averageStarted) {
    // Start from the first sample, not from 0
    s_battery.averageAcc = (int32_t)s_battery.voltage << AVERAGE_SHIFT;
    s_battery.averageStarted = true;
    s_battery.lastSampleMs = now;
    _updateLevel();
    return;
  }
  if((now - s_battery.lastSampleMs) < SAMPLE_MS)
    return;
  s_battery.lastSampleMs = now;
  s_battery.averageAcc += (int32_t)s_battery.voltage - (s_battery.averageAcc >> AVERAGE_SHIFT);
  _updateLevel();
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
  if(!_hasBattery())
    return 100;
  if(s_battery.level < 0)   // No sample yet
    return (uint8_t)((_curveTenths(s_battery.voltage) + 5) / 10);
  return (uint8_t)s_battery.level;
}

bool Battery_isCritical(void) {
  // No battery supervision for NONE and USB (the same rule as in Battery_readLevel)
  if(!_hasBattery())
    return false;
  return (s_battery.voltage <= kBatterySpecs[s_battery.type].critical);
}

// The average voltage (about 26 s), as the level uses it
uint16_t Battery_readVoltage(void) {
  return s_battery.averageStarted ? _averageVoltage() : s_battery.voltage;
}

uint8_t Battery_getType(void) {
  return s_battery.type;
}

void Battery_setType(uint8_t type) {
  // Type comes from EEPROM, it indexes kBatterySpecs
  if(type >= (sizeof(kBatterySpecs) / sizeof(kBatterySpecs[0])))
    type = BATTERY_TYPE__UNKNOWN;

  s_battery.type = type;
  // An other battery: the average and the level start again from the next sample
  s_battery.averageStarted = false;
  s_battery.level = -1;
}

uint8_t Battery_getTypeUsed(void) {
  // return (s_battery.type == BATTERY_TYPE__AUTO) ? s_battery.typeDetected : s_battery.type;
  return s_battery.type;
}

/* > Local Function Definitions *******************************************************************/

static bool _hasBattery(void) {
  return kBatterySpecs[s_battery.type].curve != NULL;
}

static uint16_t _averageVoltage(void) {
  return (uint16_t)((s_battery.averageAcc + (1 << (AVERAGE_SHIFT - 1))) >> AVERAGE_SHIFT);
}

// The level of the curve in 0.1 % (0..1000): a straight line between the two points around it
static int32_t _curveTenths(uint16_t millivolts) {
  const battery_specs_t* specs = &kBatterySpecs[s_battery.type];
  if(specs->curve == NULL)
    return 1000;
  const int32_t cells = specs->cells;
  const int32_t mv = millivolts;
  if(mv <= specs->curve[0] * cells)
    return 0;
  for(uint8_t i = 1; i < CURVE_POINTS; i++) {
    const int32_t high = specs->curve[i] * cells;
    if(mv < high) {
      const int32_t low = specs->curve[i - 1] * cells;
      return (i - 1) * 100 + (100 * (mv - low)) / (high - low);
    }
  }
  return 1000;
}

static void _updateLevel(void) {
  if(!_hasBattery())
    return;
  const int32_t tenths = _curveTenths(_averageVoltage());
  const int16_t rounded = (int16_t)((tenths + 5) / 10);
  if(s_battery.level < 0) {
    s_battery.level = rounded;
    return;
  }
  const int32_t middle = s_battery.level * 10;
  if(tenths <= middle - LEVEL_DOWN_TENTHS || tenths >= middle + LEVEL_UP_TENTHS)
    s_battery.level = rounded;
}
