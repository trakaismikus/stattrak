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
#include "launcher_view.h"
#include "app_host_view.h"
#include "app_sync_view.h"
#include "apps.h"

#include "gfx_fx.h"

#include "eeprom.h"
#include "ota.h"
#include "ble_commands.h"
#include "bt_start.h"
#include "data_store.h"
#include "app_data.h"
#include "device_log.h"
#include "start_pic.h"

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
static uint32_t s_awakeTime = 0;           // The last loop with an "awake" app open (auto-off waits)

static bool     s_lowBatteryShown = false;
static uint32_t s_lowBatteryTime;
static uint32_t s_criticalBatteryTime;

// The question "StatTrak version" and the warning "Battery type not set" come one time for each
// power-on. A person who leaves one of them with the app menu gets it again at the next power-on.
static bool     s_modelAsked = false;
static bool     s_batteryUnsetShown = false;


/* > Function Definitions *************************************************************************/

void App_init(void) {

  // Check if first boot
  if(Eeprom_read(ENTRY_ID__INIT) == 0) {
    Eeprom_writeDefaults(kEntryDefaults);
  } else {
    // Entries added by a firmware update get their default instead of 0
    Eeprom_writeMissingDefaults(kEntryDefaults);
  }

  // The app list and the installed apps (flash namespace "apps")
  Apps_init();

  // App data, the device log and the start picture (flash namespace "data", docs/DATA.md)
  DataStore_init();
  DeviceLog_init();
  StartPic_init();

  // Read all settings and apply
  App_reloadSettings();

  s_lowBatteryShown = false;

  // The app of Settings > Background starts (docs/APPS.md section 4.6)
  AppHost_backgroundRefresh();
}

// The flash values of ENTRY_LIST, for the backup (docs/DATA.md section 5). The keys are in main.c.
extern const char* kEepromEntryKeys[];

uint8_t App_getEntryCount(void) {
  return (uint8_t)ENTRY_ID_COUNT;
}

const char* App_getEntryKey(uint8_t index) {
  return (index < ENTRY_ID_COUNT) ? kEepromEntryKeys[index] : NULL;
}

uint32_t App_getEntryMax(uint8_t index) {
  return (index < ENTRY_ID_COUNT) ? kEntryMaxValues[index] : 0;
}

// The settings from flash into use (at start, and after a restore of a backup)
void App_reloadSettings(void) {
  // Replace stored values that this firmware version cannot use
  _sanitizeEntries();
  Battery_setType(App_getBatteryTypeInUse());

  SettingsView_init();

  s_brightnessSetValue = (SSD1307_MAX_BRIGHTNESS * Eeprom_read(ENTRY_ID__BRIGHTNESS)) / BRIGHTNESS_SETTING_COUNT;
  s_brightnessDimValue = (s_brightnessSetValue * Eeprom_read(ENTRY_ID__DIM_LEVEL)) / 10;
  s_brightness = s_brightnessSetValue;
  Ssd1307_setBrightness(s_brightness);
}


uint8_t App_getDeviceModel(void) {
  const uint32_t model = Eeprom_read(ENTRY_ID__DEVICE_MODEL);
  return (model <= DEVICE_MODEL__PEQ) ? (uint8_t)model : DEVICE_MODEL__UNSET;
}

// The stored battery type is in use only for DIY. A device that comes from an older firmware can
// have a type from an other list (docs/AUDIT.md, A4): it is not used before the person answered.
uint8_t App_getBatteryTypeInUse(void) {
  switch(App_getDeviceModel()) {
    case DEVICE_MODEL__DIY: return (uint8_t)Eeprom_read(ENTRY_ID__BATTERY_TYPE);
    case DEVICE_MODEL__PEQ: return BATTERY_TYPE__RCR123A;
    default:                return BATTERY_TYPE__UNKNOWN;
  }
}

void App_answerDeviceModel(uint8_t model) {
  if(model != DEVICE_MODEL__DIY && model != DEVICE_MODEL__PEQ)
    return;

  // The same answer again (Settings > Version, by mistake) changes nothing
  if(model != App_getDeviceModel()) {
    Eeprom_write(ENTRY_ID__DEVICE_MODEL, model);
    Eeprom_write(ENTRY_ID__BATTERY_TYPE, (model == DEVICE_MODEL__PEQ) ? BATTERY_TYPE__RCR123A : BATTERY_TYPE__UNKNOWN);
    Battery_setType(App_getBatteryTypeInUse());
    SettingsView_init();
    s_lowBatteryShown = false;
  }
  s_modelAsked = true;

  if(model == DEVICE_MODEL__DIY && Eeprom_read(ENTRY_ID__BATTERY_TYPE) == BATTERY_TYPE__UNKNOWN) {
    s_batteryUnsetShown = true;
    AppView_set(VIEW_ID__BATTERY_UNSET);
  } else {
    AppView_set(Launcher_getStartView());
  }
}

void App_closeBatteryUnset(bool openSetting) {
  if(openSetting && App_getDeviceModel() == DEVICE_MODEL__DIY) {
    SettingsView_select(SETTING_ID__BATTERY_TYPE);
    AppView_set(VIEW_ID__SETTINGS);
  } else {
    AppView_set(Launcher_getStartView());
  }
}

// A normal start asks here (Launcher_getStartView()), before the first app opens: an installed
// app must not get "on open", "on close" and "on open" again for one power-on.
uint8_t App_takeStartPrompt(void) {
  const uint8_t model = App_getDeviceModel();
  if(model == DEVICE_MODEL__UNSET && !s_modelAsked) {
    s_modelAsked = true;
    return VIEW_ID__DEVICE_MODEL;
  }
  if(model == DEVICE_MODEL__DIY && !s_batteryUnsetShown &&
     Eeprom_read(ENTRY_ID__BATTERY_TYPE) == BATTERY_TYPE__UNKNOWN) {
    s_batteryUnsetShown = true;
    return VIEW_ID__BATTERY_UNSET;
  }
  return VIEW_ID__BLANK;
}

// A start with a new firmware shows the accept view first, and its OK opens the kill counter
// (main/views/fw_accept_view.c): the question or the warning comes over that one.
static void _startPrompts(void) {
  if(AppView_get() != VIEW_ID__FW_ACCEPT || AppView_getNext() != VIEW_ID__TOTAL)
    return;

  const view_id_t prompt = (view_id_t)App_takeStartPrompt();
  if(prompt != VIEW_ID__BLANK)
    AppView_set(prompt);
}


void App_update(void) {
  uint32_t now = Time_getMillis();
  uint8_t otaState = Ota_getState();

  // Update Battery Level
  Battery_read();

  // Answer the Bluetooth commands of user features
  BleCommands_update();

  // The app menu opens the selected app after 3 s without a press
  Launcher_update(now);

  // Installed apps: the tick event, and the flash writes of the app host; the install question
  AppHost_update(now);
  AppSync_update(now);
  BtStart_update(now);   // The radio start of a Bluetooth screen (main/views/bt_start.h)
  DeviceLog_update(now); // The entry of this start, 5 s after it

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

  // The question "StatTrak version" and the warning "Battery type not set"
  _startPrompts();

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
  // An app with the permission "awake" (a log of many hours) keeps the device on while it is open
  // (not as activity: the dim still works)
  if(AppHost_keepsAwake())
    s_awakeTime = now;
  const uint32_t autoOffTime = TABLE_GET(kAutoOffTimes, Eeprom_read(ENTRY_ID__AUTO_OFF));
  if(autoOffTime != 0) {
    const uint32_t sinceActivity = now - s_lastActivityTime;
    const uint32_t sinceAwake = now - s_awakeTime;
    const uint32_t idleTime = (sinceAwake < sinceActivity) ? sinceAwake : sinceActivity;
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
    AppHost_onShutdown();   // The saved values of an open app
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
      // The row is in the list only for DIY (PEQ is always 1xRCR123A)
      if(App_getDeviceModel() != DEVICE_MODEL__DIY)
        break;
      Battery_setType(value);
      Eeprom_write(ENTRY_ID__BATTERY_TYPE, value);
      SettingsView_setBatteryType(value);
      break;

    case SETTING_ID__VERSION:
      // A hold on the row Version: the question "StatTrak version" again (a wrong answer can be
      // repaired without a factory reset). All data stays.
      AppView_set(VIEW_ID__DEVICE_MODEL);
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

    case SETTING_ID__APP_SYNC:
      AppView_set(VIEW_ID__APP_SYNC);
      break;

    case SETTING_ID__BACKGROUND:
      // value = the place in the list of the row (OFF, then the apps that can run in the background)
      Apps_setBackground(SettingsView_getBackgroundApp(value));
      AppHost_backgroundRefresh();
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

void App_tone(uint16_t freq, uint16_t ms) {
  if(Eeprom_read(ENTRY_ID__SOUND) > 0) {
    Buzzer_tone(freq, ms);
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
    // The app storage is in the same flash: close it before the erase (App_init opens it again)
    AppHost_backgroundForget();
    Apps_deinit();
    DataStore_deinit();
    AppData_forget();     // The app data and the start picture go; App_init writes the device log back
    Eeprom_format(kEntryDefaults);

    // TODO: reset!
    App_init();

    // The device version is "not set" again: the question comes at the next power-on
    s_modelAsked = true;
    s_batteryUnsetShown = true;
  }

  AppView_set(confirm ? VIEW_ID__TOTAL : VIEW_ID__SETTINGS);
}