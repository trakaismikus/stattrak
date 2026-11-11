/***************************************************************************************************
 * @file    : app.c
 * @brief   : Application source file
 * @author  : Janis Zaharans
 * @date    : Apr 8, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app.h"
#include "board.h"

#include "battery.h"
#include "ssd1307.h"

#include "app_view.h"
#include "ask_view.h"
#include "settings_view.h"

#include "gfx_fx.h"

#include "eeprom.h"
#include "ota.h"
#include "ble_commands.h"

#include <assert.h>

#include "time.h"


static void _factoryReset(bool confirm);
static void _gameReset(bool confirm);
static void _sanitizeEntries(void);


/* > Defines **************************************************************************************/

#define BRIGHTNESS_SETTING_COUNT  (10)

#define LOW_BATTERY_PERIOD_MS       TIME_MIN(1)
#define CRITICAL_BATTERY_PERIOD_MS  TIME_MIN(1)

/* > Macros ***************************************************************************************/
#define elemof(_x)      ((sizeof(_x)/sizeof((_x)[0])))

// Table lookup with an index from EEPROM, falls back to the first entry if out of range
#define TABLE_GET(_table, _idx)  ((_table)[((_idx) < elemof(_table)) ? (_idx) : 0])

#define TIME_SEC(_sec)  ((_sec) * 1000UL)
#define TIME_MIN(_min)  ((_min) * 60UL * 1000UL)
#define TIME_H(_h)      ((_h) * 3600UL * 1000UL)

/* > Constants ************************************************************************************/

static const char* kGameResetStr = "Reset Game Counter?";
static const char* kFactoryResetStr = "Do a Factory Reset?";

// If 0 - OFF
static const uint32_t kDimTimes[] = {
  0, TIME_SEC(5), TIME_SEC(10), TIME_SEC(30),
  TIME_MIN(1), TIME_MIN(2), TIME_MIN(5), TIME_MIN(10),
};

static const uint32_t kAutoOffTimes[] = {
  0, TIME_MIN(30), TIME_H(1), TIME_H(2), TIME_H(6), TIME_H(12),
};

static const int8_t kLowBatThresholds[] = {
  [LOW_BAT__OFF] = -1, // Disabled
  [LOW_BAT__0]   = 0,
  [LOW_BAT__10]  = 10,
  [LOW_BAT__20]  = 20,
  [LOW_BAT__30]  = 30,
};

static const uint32_t kEntryDefaults[] = {
  #define ENTRY(_id, _key, _default, _max) [ENTRY_ID__ ## _id] = _default,
    ENTRY_LIST
  #undef ENTRY
};

// Largest value of each entry that this firmware can use
static const uint32_t kEntryMaxValues[] = {
  #define ENTRY(_id, _key, _default, _max) [ENTRY_ID__ ## _id] = _max,
    ENTRY_LIST
  #undef ENTRY
};

static_assert(elemof(kEntryDefaults) == ENTRY_ID_COUNT, "All Entries should have a default value");
static_assert(elemof(kEntryMaxValues) == ENTRY_ID_COUNT, "All Entries should have a maximum value");

// The tables above are read with an index from flash. The limits in ENTRY_LIST must fit them.
static_assert(elemof(kDimTimes) == (ENTRY_MAX__DIM_SCREEN + 1), "kDimTimes must have one value for each DIM_SCREEN setting");
static_assert(elemof(kAutoOffTimes) == (ENTRY_MAX__AUTO_OFF + 1), "kAutoOffTimes must have one value for each AUTO_OFF setting");
static_assert(elemof(kLowBatThresholds) == (ENTRY_MAX__LOW_BAT + 1), "kLowBatThresholds must have one value for each LOW_BAT setting");
static_assert(BRIGHTNESS_SETTING_COUNT == ENTRY_MAX__BRIGHTNESS, "The brightness setting range changed");
static_assert(ENTRY_ID_COUNT <= 32, "EEPROM missing entry mask supports up to 32 entries");

/* > Local Variables ******************************************************************************/

static uint8_t s_dim = 0;
static uint16_t s_brightness = 0x48;
static uint16_t s_brightnessSetValue;
static uint16_t s_brightnessDimValue;

static uint8_t s_shutdownReq = 0;

static uint32_t s_lastActivityTime = 0;

static bool     s_lowBatteryShown = false;
static uint32_t s_lowBatteryTime;
static uint32_t s_criticalBatteryTime;


/* > Function Definitions *************************************************************************/

void App_init(void) {

  // Check if first boot
  if(Eeprom_read(ENTRY_ID__INIT) == 0) {
    Eeprom_writeDefaults(kEntryDefaults);
  } else {
    // Entries added by a firmware update get their default instead of 0
    Eeprom_writeMissingDefaults(kEntryDefaults);
  }

  // Replace stored values that this firmware version cannot use
  _sanitizeEntries();

  // Read all settings and apply
  Battery_setType(Eeprom_read(ENTRY_ID__BATTERY_TYPE));

  SettingsView_init();

  s_brightnessSetValue = (SSD1307_MAX_BRIGHTNESS * Eeprom_read(ENTRY_ID__BRIGHTNESS)) / BRIGHTNESS_SETTING_COUNT;
  s_brightnessDimValue = (s_brightnessSetValue * Eeprom_read(ENTRY_ID__DIM_LEVEL)) / 10;
  s_brightness = s_brightnessSetValue;
  Ssd1307_setBrightness(s_brightness);

  s_lowBatteryShown = false;
}


void App_update(void) {
  uint32_t now = Time_getMillis();
  uint8_t otaState = Ota_getState();

  // Update Battery Level
  Battery_read();

  // Answer the Bluetooth commands of user features
  BleCommands_update();

  // While firmware is being received, count it as activity, so auto off and dim start after it
  if(Ota_isBusy(otaState)) {
    App_updateActivity(now);
  }

  // Check if battery is critical. Not while firmware is being received: a device that reads
  // "critical" by mistake (wrong battery type, bad firmware) must still be able to get a new
  // firmware. A transfer that stops because the battery is empty does no harm.
  if(Battery_isCritical() && !Ota_isBusy(otaState)) {
    if((now - s_criticalBatteryTime) >= (CRITICAL_BATTERY_PERIOD_MS - TIME_SEC(11))) {
      AppView_set(VIEW_ID__BATTERY_CRITICAL);
    }

    // Start critical battery timer
    if((now - s_criticalBatteryTime) >= CRITICAL_BATTERY_PERIOD_MS) {
      App_requestShutdown();
    }
  } else {
    // Reset critical battery timer
    s_criticalBatteryTime = now;
  }

  // Allow low battery warning only when OTA is not active
  if(!Ota_isBusy(otaState) && !s_lowBatteryShown) {
    // Check if battery is low. Allow to show warning only once.
    const int8_t lowBatThreshold = TABLE_GET(kLowBatThresholds, Eeprom_read(ENTRY_ID__LOW_BAT));
    if((int8_t)Battery_readLevel() <= lowBatThreshold) {
      // Start low battery timer
      if((now - s_lowBatteryTime) >= LOW_BATTERY_PERIOD_MS) {
        s_lowBatteryShown = true;
        if(!AppView_isActive(VIEW_ID__BATTERY_CRITICAL))
          AppView_set(VIEW_ID__BATTERY_LOW);
      }
    } else {
      // Reset low battery timer
      s_lowBatteryTime = now;
    }
  }

  // Auto power off
  const uint32_t autoOffTime = TABLE_GET(kAutoOffTimes, Eeprom_read(ENTRY_ID__AUTO_OFF));
  if(autoOffTime != 0) {
    const uint32_t idleTime = now - s_lastActivityTime;
    if(idleTime > autoOffTime) {
      App_requestShutdown();
    } else if(idleTime >= (autoOffTime - TIME_MIN(5)) &&
              !AppView_isActive(VIEW_ID__AUTO_OFF) && !AppView_isActive(VIEW_ID__BATTERY_CRITICAL)) {
      // Critical battery warning has priority over auto off warning
      AppView_set(VIEW_ID__AUTO_OFF);
    }
  }

  // Dim
  const uint32_t dimTime = TABLE_GET(kDimTimes, Eeprom_read(ENTRY_ID__DIM_SCREEN));
  if(dimTime != 0 && (now - s_lastActivityTime) > dimTime) {
    if(s_brightness > s_brightnessDimValue) {
      s_brightness--;
      Ssd1307_setBrightness(s_brightness);
      s_dim = 1;
    }
  } else if(s_dim == 1) {
    s_dim = 0;
    s_brightness = s_brightnessSetValue;
    Ssd1307_setBrightness(s_brightness);
  }


  // Process Shutdown Request
  if(s_shutdownReq == 1) {
    s_shutdownReq = 2;
    App_click(800);
    GfxFx_startCollapse();
  } else if(s_shutdownReq == 2 && GfxFx_isCollapsed()) {
    s_shutdownReq = 0;

    Gfx_clearAll(0);
    Gfx_updateContext();

    Board_powerOff();
    while(1);
  }
}


void App_updateSetting(uint8_t id, uint32_t value) {

  switch(id) {
    case SETTING_ID__GAME_RESET:
      AskView_set(kGameResetStr, _gameReset);
      AppView_set(VIEW_ID__ASK);
      break;
    
    case SETTING_ID__BATTERY_TYPE: 
      Battery_setType(value);
      Eeprom_write(ENTRY_ID__BATTERY_TYPE, value);
      SettingsView_setBatteryType(value);
      break;
    
    case SETTING_ID__SHOW_BATTERY:
      Eeprom_write(ENTRY_ID__SHOW_BATTERY, value);
      break;

    case SETTING_ID__LOW_BAT:
      Eeprom_write(ENTRY_ID__LOW_BAT, value);
      break;
    
    case SETTING_ID__BRIGHTNESS: 
      s_brightnessSetValue = (SSD1307_MAX_BRIGHTNESS * value) / BRIGHTNESS_SETTING_COUNT;
      s_brightnessDimValue = (s_brightnessSetValue * Eeprom_read(ENTRY_ID__DIM_LEVEL)) / 10;
      s_brightness = s_brightnessSetValue;
      Ssd1307_setBrightness(s_brightness);
      Eeprom_write(ENTRY_ID__BRIGHTNESS, value);
      break;
    
    case SETTING_ID__DIM_SCREEN: 
      Eeprom_write(ENTRY_ID__DIM_SCREEN, value);
      break;

    case SETTING_ID__DIM_LEVEL:
      s_brightnessDimValue = (s_brightnessSetValue * value) / 10;
      Eeprom_write(ENTRY_ID__DIM_LEVEL, value);
      break;
    
    case SETTING_ID__AUTO_OFF:
      Eeprom_write(ENTRY_ID__AUTO_OFF, value);
      break;

    case SETTING_ID__COUNTER:
      Eeprom_write(ENTRY_ID__COUNTER, value);
      break;

    case SETTING_ID__SOUND:
      Eeprom_write(ENTRY_ID__SOUND, value);
      break;
    
    case SETTING_ID__FACTORY_RESET: 
      AskView_set(kFactoryResetStr, _factoryReset);
      AppView_set(VIEW_ID__ASK);
      break;

    case SETTING_ID__FW_UPDATE:
      AppView_set(VIEW_ID__UPDATE);
      break;

    default: break;
  }

}

void App_updateActivity(uint32_t time) {
  s_lastActivityTime = time;
}

uint32_t App_getLastActivityTime(void) {
  return s_lastActivityTime;
}


void App_requestShutdown(void) {
  if(s_shutdownReq == 0) {
    s_shutdownReq = 1;
  }
}




void App_click(uint16_t freq) {
  if(Eeprom_read(ENTRY_ID__SOUND) > 0) {
    Buzzer_click(freq);
  }
}



// A value in flash can come from an other firmware version, or the flash can be damaged.
// A value that this firmware cannot use is replaced with the default value.
static void _sanitizeEntries(void) {
  for(uint32_t id = 0; id < ENTRY_ID_COUNT; id++) {
    if(Eeprom_read(id) > kEntryMaxValues[id]) {
      Eeprom_write(id, kEntryDefaults[id]);
    }
  }
}

static void _gameReset(bool confirm) {

  // TODO: do game reset
  if(confirm) {
    Eeprom_write(ENTRY_ID__GAME_KILLS, 0);
    Eeprom_write(ENTRY_ID__GAME_DEATHS, 0);
  }

  AppView_set(confirm ? VIEW_ID__TOTAL : VIEW_ID__SETTINGS);
}

static void _factoryReset(bool confirm) {

  // TODO: Do Factory reset
  if(confirm) {
    Eeprom_format(kEntryDefaults);

    // TODO: reset!
    App_init();
  }

  AppView_set(confirm ? VIEW_ID__TOTAL : VIEW_ID__SETTINGS);
}