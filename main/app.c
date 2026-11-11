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

#include <assert.h>

#include "time.h"


static void _factoryReset(bool confirm);
static void _gameReset(bool confirm);


/* > Defines **************************************************************************************/

#define BRIGHTNESS_SETTING_COUNT  (10)

#define LOW_BATTERY_PERIOD_MS       TIME_MIN(1)
#define CRITICAL_BATTERY_PERIOD_MS  TIME_MIN(1)

/* > Macros ***************************************************************************************/
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
  #define ENTRY(_id, _key, _default) [ENTRY_ID__ ## _id] = _default,
    ENTRY_LIST
  #undef ENTRY
};

static_assert((sizeof(kEntryDefaults)/sizeof(kEntryDefaults[0])) == ENTRY_ID_COUNT, "All Entries should have a default value");

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
  }

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

  // Allow battery warnings && auto off only when OTA is not active
  if(otaState == OTA_STATE__IDLE) {
    // Check if battery is low. Allow to show warning only once.
    if(!s_lowBatteryShown) {
      const int8_t lowBatThreshold = kLowBatThresholds[Eeprom_read(ENTRY_ID__LOW_BAT)];
      if((int8_t)Battery_readLevel() <= lowBatThreshold) {
        // Start low battery timer
        if((now - s_lowBatteryTime) >= LOW_BATTERY_PERIOD_MS) {
          s_lowBatteryShown = true;
          if(AppView_get() != VIEW_ID__BATTERY_CRITICAL)
            AppView_set(VIEW_ID__BATTERY_LOW);
        }
      } else {
        // Reset low battery timer
        s_lowBatteryTime = now;
      }
    }


    // Check if battery is critical
    if(Battery_isCritical()) {
      if((now - s_criticalBatteryTime) >= (CRITICAL_BATTERY_PERIOD_MS - TIME_SEC(11)) &&
        AppView_get() != VIEW_ID__BATTERY_CRITICAL) {
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


    // Auto power off
    uint32_t autoOffTime = kAutoOffTimes[Eeprom_read(ENTRY_ID__AUTO_OFF)];
    if(autoOffTime != 0) {
      if((now - s_lastActivityTime) >= (autoOffTime - TIME_MIN(5)) && AppView_get() != VIEW_ID__AUTO_OFF) {
        AppView_set(VIEW_ID__AUTO_OFF);
      } else if((now - s_lastActivityTime) > autoOffTime) {
        App_requestShutdown();
      }
    }
  }

  // Dim
  const uint32_t dimTime = kDimTimes[Eeprom_read(ENTRY_ID__DIM_SCREEN)];
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