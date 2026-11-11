/***************************************************************************************************
 * @file    : app.h
 * @brief   : Application header file
 * @author  : Janis Zaharans
 * @date    : Apr 8, 2022
 **************************************************************************************************/

#ifndef _APP_H
#define _APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include "app_view.h"
#include "battery.h"

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

#define IF(_cond, ...) IF_IMPL(_cond, __VA_ARGS__)
#define IF_IMPL(_cond, ...) _IF_##_cond(__VA_ARGS__)
#define _IF_0(...)
#define _IF_1(...) __VA_ARGS__

/* > Enumerations *********************************************************************************/

#ifdef OLD_EVENTS
#define EVENT_COUNT         (12)

enum event_enum {
  EVENT__PRESS_1      = 0x0001,
  EVENT__PRESS_2      = 0x0002,
  EVENT__LONG_PRESS_1 = 0x0004,
  EVENT__LONG_PRESS_2 = 0x0008,
  EVENT__LONG_HOLD_1  = 0x0010,
  EVENT__LONG_HOLD_2  = 0x0020,
  EVENT__PRESS_BOTH   = 0x0040,
  EVENT__LONG_PRESS_B = 0x0080,

  EVENT__RAW_PRESS_1    = 0x0100,
  EVENT__RAW_PRESS_2    = 0x0200,
  EVENT__RAW_RELEASE_1  = 0x0400,
  EVENT__RAW_RELEASE_2  = 0x0800,
};
#else
  typedef enum event_e {
    EVENT__NONE = 0,
    EVENT__PRESS_1,
    EVENT__PRESS_2,
    EVENT__LONG_PRESS_1,
    EVENT__LONG_PRESS_2,
    EVENT__LONG_HOLD_1,
    EVENT__LONG_HOLD_2,
    EVENT__PRESS_BOTH,
    EVENT__LONG_PRESS_B,
    EVENT__RAW_PRESS_1,
    EVENT__RAW_PRESS_2,
    EVENT__RAW_RELEASE_1,
    EVENT__RAW_RELEASE_2,

    EVENT_COUNT
  } event_t;
#endif


// Values that stay in flash (NVS) when the power is off.
//
// ENTRY(_id, _key, _default, _max)
//   _key      Name in flash. 15 characters at most. Never change or reuse the key of a released entry.
//   _default  Value on the first start and after a factory reset.
//   _max      Largest value that this firmware can use. A larger value in flash (from an other
//             firmware version, or from damaged flash) is replaced with _default at start.
//
// A device that already has an older firmware reads 0 for a new entry. Make sure that 0 is a
// safe value for each entry that you add.
#define ENTRY_LIST \
  ENTRY(INIT,          init,          1,                      1) \
  ENTRY(TOTAL_KILLS,   total_kills,   0,                      0xFFFFFFFF) \
  ENTRY(TOTAL_DEATHS,  total_deaths,  0,                      0xFFFFFFFF) \
  ENTRY(GAME_KILLS,    game_kills,    0,                      0xFFFFFFFF) \
  ENTRY(GAME_DEATHS,   game_deaths,   0,                      0xFFFFFFFF) \
  ENTRY(BATTERY_TYPE,  battery_type,  BATTERY_TYPE__UNKNOWN,  BATTERY_TYPE__NIMH_8S) \
  ENTRY(SHOW_BATTERY,  show_battery,  0,                      1) \
  ENTRY(BRIGHTNESS,    brightness,    8, /* 80% */            10) \
  ENTRY(DIM_SCREEN,    dim_screen,    DIM__30_SEC,            DIM__10_MIN) \
  ENTRY(DIM_LEVEL,     dim_level,     2, /* 20% */            9) \
  ENTRY(AUTO_OFF,      auto_off,      AUTO_OFF__OFF,          AUTO_OFF__12_H) \
  ENTRY(COUNTER,       counter,       0, /* Totals or Game */ COUNTER__GAME) \
  ENTRY(SOUND,         sound,         1,                      1) \
  ENTRY(LOW_BAT,       low_bat,       LOW_BAT__OFF,           LOW_BAT__30)

typedef enum entry_id_enum {
  #define ENTRY(_id, _key, _default, _max) ENTRY_ID__ ## _id,
    ENTRY_LIST
  #undef ENTRY

  // Count of Memory entries
  ENTRY_ID_COUNT
} entry_id_t;


#define SETTING_STR_VALUES(...) { __VA_ARGS__ }

// SETTING(_id, _name, _str, _nonvolatile, _cycle, _values)
#define SETTING_LIST \
  SETTING(GAME_RESET,   Game,         "Game",       0, false,  SETTING_STR_VALUES("RESET")) \
  SETTING(COUNTER,      Counter,      "Counter",    1, true,   SETTING_STR_VALUES("TOTAL", "GAME")) \
  SETTING(BATTERY_TYPE, Battery,      "Battery",    1, true,   SETTING_STR_VALUES("NONE", "USB", "1xRCR123A","3.7V(1S)", "7.4V(2S)", "11.1V(3S)", "3xAA/AAA", "9.6V(NiMH)")) \
  SETTING(AUTO_OFF,     AutoOff,      "Auto Off",   1, false,  SETTING_STR_VALUES("OFF", "30 min", "1 h", "2 h", "6 h", "12 h")) \
  SETTING(SHOW_BATTERY, ShowBat,      "Show Bat",   1, true,   SETTING_STR_VALUES("OFF", "ON")) \
  SETTING(LOW_BAT,      LowBat,       "Low Bat",    1, false,  SETTING_STR_VALUES("OFF", "0%", "10%", "20%", "30%")) \
  SETTING(BRIGHTNESS,   Brightness,   "Brightness", 1, false,  SETTING_STR_VALUES("1%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%")) \
  SETTING(DIM_SCREEN,   Dim,          "Dim Screen", 1, false,  SETTING_STR_VALUES("OFF", "5 sec", "10 sec", "30 sec", "1 min", "2 min", "5 min", "10 min")) \
  SETTING(DIM_LEVEL,    DimLevel,     "Dim Level",  1, false,  SETTING_STR_VALUES("1%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%")) \
  SETTING(SOUND,        Sound,        "Sound",      1, true,   SETTING_STR_VALUES("OFF", "ON")) \
  SETTING(APP_SYNC,     Apps,         "Apps",       0, false,  SETTING_STR_VALUES("SYNC")) \
  SETTING(FACTORY_RESET, Factory,     "Factory",    0, false,  SETTING_STR_VALUES("RESET")) \
  SETTING(FW_UPDATE,    Firmware,     "Firmware",   0, false,  SETTING_STR_VALUES("UPDATE")) \
  SETTING(VERSION,      Version,      "Version",    0, false,  SETTING_STR_VALUES(s_fwVersion)) \
  SETTING(SERIAL,       Serial,       "Serial",     0, false,  SETTING_STR_VALUES(s_serialNumber))


typedef enum setting_id_enum {
  #define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) SETTING_ID__ ## _id,
    SETTING_LIST
  #undef SETTING

  // Count of Setting entries
  SETTING_ID_COUNT
} setting_id_t;


enum dim_enum {
  DIM__OFF = 0,
  DIM__5_SEC,
  DIM__10_SEC,
  DIM__30_SEC,
  DIM__1_MIN,
  DIM__2_MIN,
  DIM__5_MIN,
  DIM__10_MIN,
};

enum auto_off_enum {
  AUTO_OFF__OFF = 0,
  AUTO_OFF__30_MIN,
  AUTO_OFF__1_H,
  AUTO_OFF__2_H,
  AUTO_OFF__6_H,
  AUTO_OFF__12_H,
};

enum low_bat_enum {
  LOW_BAT__OFF = 0,
  LOW_BAT__0,
  LOW_BAT__10,
  LOW_BAT__20,
  LOW_BAT__30,
};

enum counter_enum {
  COUNTER__TOTAL = 0,
  COUNTER__GAME,
};

// Largest valid value of each entry, as a compile-time constant: ENTRY_MAX__<id>
// (0xFFFFFFFF does not fit in an enum and becomes -1. It is used for settings only.)
enum entry_max_enum {
  #define ENTRY(_id, _key, _default, _max) ENTRY_MAX__ ## _id = (int)(_max),
    ENTRY_LIST
  #undef ENTRY
};

/* > Function Declaration *************************************************************************/

void App_init(void);
void App_update(void);

void App_updateSetting(uint8_t id, uint32_t value);
void App_updateActivity(uint32_t time);
uint32_t App_getLastActivityTime(void);

void App_click(uint16_t ferq);

void App_requestShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // _APP_H