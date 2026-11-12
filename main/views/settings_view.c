/***************************************************************************************************
 * @file    : settings_view.c
 * @brief   : Settings View source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "animation.h"
#include "app.h"
#include "eeprom.h"
#include "board.h"
#include "battery.h"
#include "settings_view.h"
#include "time.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);

static void _setupAnimation(animation_t * animation);
static void _updateAnimation(animation_t * animation, uint32_t progress);

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

#define elemof(_x)  ((sizeof(_x)/sizeof((_x)[0])))

#define SETTING_GET_ID(_idx)        (s_settingOrderList[_idx])
#define SETTING_GET_LABEL(_id)      (kSettingEntries[_id].label)
#define SETTING_GET_VALUE_STR(_id)  (kSettingEntries[_id].valueStrs[s_settingValues[_id]])

/* > Type Definitions *****************************************************************************/

typedef struct {
  int16_t animOffset;
  int16_t animOldOffset;
  int16_t idx;
} settings_view_t;

typedef struct {
  const char* label;
  const char** valueStrs;
  const bool cycle;
  uint8_t valueCount;
} setting_entry_t;

typedef struct {
  int16_t value;
  int16_t offset;
  int16_t oldOffset;
} anim_value_t;

/* > Constants ************************************************************************************/

static const animation_functions_t kAnimationFunc = {
  .setup = _setupAnimation,
  .update = _updateAnimation,
};


static char s_serialNumber[16] = "-";
static char s_fwVersion[16] = "-";

// Generate setting value strings
#define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) const char* k##_name##Values[] = _values;
  SETTING_LIST
#undef SETTING


static setting_entry_t kSettingEntries[] = {
  #define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) \
    { .label = _str ":", \
      .cycle = _cycle, \
      .valueStrs = k##_name##Values, \
      .valueCount = elemof(k##_name##Values) },
    SETTING_LIST
  #undef SETTING
};

// Each stored setting must have one text for each value that ENTRY_LIST (app.h) allows.
#define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) \
  IF(_nonvolatile, static_assert(elemof(k##_name##Values) == (ENTRY_MAX__##_id + 1), \
    "Setting " _str ": the count of values is not the same as the limit in ENTRY_LIST");)
  SETTING_LIST
#undef SETTING

const struct setting_eeprom_map_struct {
  uint8_t settingId;
  uint8_t eepromId;
} kSettingEepromIdMap[] = {
  #define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) IF(_nonvolatile, { SETTING_ID__##_id, ENTRY_ID__##_id },)
    SETTING_LIST
  #undef SETTING
};


/* > Local Variables ******************************************************************************/
static settings_view_t s_settingsView;

static anim_value_t s_animValue;
static animation_t s_anim = {
  .func = &kAnimationFunc,
  .curve = ANIMATION_CURVE__EASE_OUT,
  .duration = ANIMATION_MS_TO_TICKS(300),
  .playCount = 1,
  .ctx = &s_animValue,
};

static anim_value_t s_animParamValue;
static animation_t s_animParam = {
  .func = &kAnimationFunc,
  .curve = ANIMATION_CURVE__EASE_OUT,
  .duration = ANIMATION_MS_TO_TICKS(300),
  .playCount = 1,
  .ctx = &s_animParamValue,
};

static int32_t s_settingValues[SETTING_ID_COUNT];

static uint8_t s_settingOrderList[SETTING_ID_COUNT] = {
  // Generate initial order
  #define SETTING(_id, _name, _str, _nonvolatile, _cycle, _values) SETTING_ID__##_id,
    SETTING_LIST
  #undef SETTING
};
static uint8_t s_settingCount = elemof(s_settingOrderList);


/* > Global Variables ******************************************************************************/
view_t g_settingsView = {
  .ctx = &s_settingsView,
  .eventHandler = _eventHandler,
  .draw = _draw,
};


static inline const char* _getNextValueStr(uint8_t settingId) {
  int32_t value = s_settingValues[settingId] + 1;
  if(value >= kSettingEntries[settingId].valueCount)
    value = 0;
  return (char*)kSettingEntries[settingId].valueStrs[value];
}

static inline const char* _getPrevValueStr(uint8_t settingId) {
  int32_t value = s_settingValues[settingId] - 1;
  if(value < 0)
    value = kSettingEntries[settingId].valueCount - 1;
  return (char*)kSettingEntries[settingId].valueStrs[value];
}

/* > Local Function Definitions *******************************************************************/

void SettingsView_init(void) {
  uint32_t i;
  
  for(i=0; i<elemof(kSettingEepromIdMap); i++) {
    uint8_t settingId = kSettingEepromIdMap[i].settingId;
    s_settingValues[settingId] = Eeprom_read(kSettingEepromIdMap[i].eepromId);

    // Sanity check
    if(s_settingValues[settingId] < 0)  // This should not happen
      s_settingValues[settingId] = 0;
    if(s_settingValues[settingId] >= kSettingEntries[settingId].valueCount)
      s_settingValues[settingId] = 0;

    // This should not happen
    if(s_settingValues[settingId] >= kSettingEntries[settingId].valueCount)
      s_settingValues[settingId] = 0;
  }

  // If battery type is NONE or USB, remove it from setting order list
  SettingsView_setBatteryType(Eeprom_read(ENTRY_ID__BATTERY_TYPE));

  // Get FW Version
  const char* fwVer = Board_getFwVersion();
  snprintf(s_fwVersion, sizeof(s_fwVersion), "%s", fwVer);

  // Get Serial Number
  uint8_t uid[6];
  Board_getUID(uid);
  snprintf(s_serialNumber, sizeof(s_serialNumber), "%02X%02X%02X%02X%02X%02X",
    uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);
}


uint8_t SettingsView_getSelectedId(void) {
  return SETTING_GET_ID(s_settingsView.idx);
}


void SettingsView_setBatteryType(uint8_t type) {
  // If battery type is NONE or USB, remove it from setting order list
  if(type <= BATTERY_TYPE__USB && s_settingCount == elemof(s_settingOrderList)) {
    // Remove Show Battery and Low Battery settings
    memmove(&s_settingOrderList[SETTING_ID__SHOW_BATTERY], &s_settingOrderList[SETTING_ID__BRIGHTNESS], (SETTING_ID_COUNT - SETTING_ID__BRIGHTNESS) * sizeof(s_settingOrderList[0]));
    s_settingCount -= 2;
  } else {
    // Add back Show Battery and Low Battery settings
    if(type > BATTERY_TYPE__USB && s_settingCount < elemof(s_settingOrderList)) {
      memmove(&s_settingOrderList[SETTING_ID__BRIGHTNESS], &s_settingOrderList[SETTING_ID__SHOW_BATTERY], (SETTING_ID_COUNT - SETTING_ID__BRIGHTNESS) * sizeof(s_settingOrderList[0]));
      s_settingOrderList[SETTING_ID__SHOW_BATTERY] = SETTING_ID__SHOW_BATTERY;
      s_settingOrderList[SETTING_ID__LOW_BAT] = SETTING_ID__LOW_BAT;
      s_settingCount = elemof(s_settingOrderList);
    }
  }
}


static bool _eventHandler(uint16_t event) {
  (void)event;
  settings_view_t * self = &s_settingsView;

  switch(event) {
    
    case EVENT__PRESS_2:
      if(self->idx < (s_settingCount - 1)) {
        self->idx++;

        s_animValue.value = self->idx * 256;
        Animation_start(&s_anim);
        App_click(1000);
      }
      return true;

    case EVENT__PRESS_1: 
      // Up from the first setting goes to the last one (Version, Serial). The list scrolls there.
      self->idx = (self->idx > 0) ? (self->idx - 1) : (s_settingCount - 1);

      s_animValue.value = self->idx * 256;
      Animation_start(&s_anim);
      App_click(1000);
      return true;

    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_HOLD_1: {
      const uint8_t id = SETTING_GET_ID(self->idx);
      setting_entry_t* entry = &kSettingEntries[id];
      int32_t* valuePtr = &s_settingValues[id];
      
      if(entry->cycle) {
        (*valuePtr)--;
        if(*valuePtr < 0)
          *valuePtr = entry->valueCount - 1;

        s_animParamValue.offset = 0;
        s_animParamValue.value = -256;
        Animation_start(&s_animParam);
        App_click(1200);
      } else {
        if(*valuePtr > 0) {
          (*valuePtr)--;

          s_animParamValue.offset = 0;
          s_animParamValue.value = -256;
          Animation_start(&s_animParam);
          App_click(1200);
        }
      }
      App_updateSetting(id, *valuePtr);
    } return true;

    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_2: {
      const uint8_t id = SETTING_GET_ID(self->idx);
      setting_entry_t* entry = &kSettingEntries[id];
      int32_t* valuePtr = &s_settingValues[id];

      if(entry->cycle) {
        (*valuePtr)++;
        if(*valuePtr >= entry->valueCount)
          *valuePtr = 0;

        s_animParamValue.offset = 0;
        s_animParamValue.value = 256;
        Animation_start(&s_animParam);
        App_click(1200);
      } else {
        if(*valuePtr < (entry->valueCount - 1)) {
          (*valuePtr)++;

          s_animParamValue.offset = 0;
          s_animParamValue.value = 256;
          Animation_start(&s_animParam);
          App_click(1200);
        }
      }
      App_updateSetting(id, *valuePtr);
    } return true;

    default: break;
  }

  return false;
}


static void _draw(void) {
  int32_t offset;
  uint32_t animOffset = (uint32_t)(s_animValue.offset >> 8);
  int32_t idx = animOffset;

  const int16_t y1 = 33-6-3; // 11
  const int16_t y2 = 20-6-3; // 20
  const int16_t y3 = 11-6-3; // 33
  
  // Never read after the end of the setting list
  if(idx >= s_settingCount)
    idx = s_settingCount - 1;

  // Neighbour ids are only drawn when in range, clamp so they are never read out of bounds
  const uint8_t id_m1 = (idx > 0) ? SETTING_GET_ID(idx - 1) : SETTING_GET_ID(idx);
  const uint8_t id_0  = SETTING_GET_ID(idx + 0);
  const uint8_t id_p1 = ((idx + 1) < s_settingCount) ? SETTING_GET_ID(idx + 1) : id_0;
  const uint8_t id_p2 = ((idx + 2) < s_settingCount) ? SETTING_GET_ID(idx + 2) : id_0;

  offset = (9 * (s_animValue.offset - (idx * 256))) >> 8;
  Gfx_setWindow(0, y1-4, 128, 11);
  if((idx + 1) < s_settingCount) {
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 0, y1 - offset, SETTING_GET_LABEL(id_p1), &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y1 - offset, SETTING_GET_VALUE_STR(id_p1), &font3x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
  if((idx + 2) < s_settingCount) {
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 0, y1 - offset + 9, SETTING_GET_LABEL(id_p2), &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y1 - offset + 9, SETTING_GET_VALUE_STR(id_p2), &font3x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
  Gfx_unsetWindow();


  Gfx_fillRect(0, y2-2, 128, 13, 1);

  idx = animOffset;
  offset = (13 * (s_animValue.offset - (idx * 256))) >> 8;
  Gfx_setWindow(0, y2-2, 128, 13);
  Gfx_drawStringAligned(Gfx_getWidth()/2 + 3, y2 - offset+2, SETTING_GET_LABEL(id_0), &font5x5, 0, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);

  if(s_animParam.active) {
    int16_t paramOffset = (13 * (s_animParamValue.offset)) >> 8;
    if(s_animParamValue.value > 0) {
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2 - paramOffset, _getPrevValueStr(id_0), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2 + 13 - paramOffset, SETTING_GET_VALUE_STR(id_0), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
    } else {
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2 - paramOffset, _getNextValueStr(id_0), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2 - 13 - paramOffset, SETTING_GET_VALUE_STR(id_0), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
    }
  } else {
    // Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2, SETTING_GET_VALUE_STR(id_0), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
    const char* valueStr = SETTING_GET_VALUE_STR(id_0);
    uint8_t strLen = strlen(valueStr);
    if(strLen > 10) {
      int32_t xStrOffset = 0;
      uint32_t xScrollTime = (((Time_getMillis() - App_getLastActivityTime()) >> 4) & 0x1FF);
      if(xScrollTime >= 128 && xScrollTime < 256) {
        xStrOffset = (((strLen - 9) * font5x5.x_advance) * (xScrollTime - 128)) >> 7;
      } else if(xScrollTime >= 256) {
        xStrOffset = ((strLen - 9) * font5x5.x_advance);
      }
      
      Gfx_setWindow(Gfx_getWidth()/2 + 6, y2 - offset+2, 60, 7);
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6 - xStrOffset, y2 - offset+2, valueStr, &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
      Gfx_unsetWindow();
    } else {
      Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset+2, valueStr, &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
    }
  }

  if((idx + 1) < s_settingCount) {
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 3, y2 - offset + 13+2, SETTING_GET_LABEL(id_p1), &font5x5, 0, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y2 - offset + 13+2, SETTING_GET_VALUE_STR(id_p1), &font5x5, 0, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
  Gfx_unsetWindow();
  

  offset = (9 * (s_animValue.offset - (idx * 256))) >> 8;
  Gfx_setWindow(0, y3-4, 128, 11);
  if((idx - 1) >= 0) {
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 0, y3 - offset, SETTING_GET_LABEL(id_m1), &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y3 - offset, SETTING_GET_VALUE_STR(id_m1), &font3x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
  if(idx >= 0) {
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 0, y3 - offset + 9, SETTING_GET_LABEL(id_0), &font3x5, 1, 0, GFX_ALIGN__RIGHT, GFX_ROTATION__0);
    Gfx_drawStringAligned(Gfx_getWidth()/2 + 6, y3 - offset + 9, SETTING_GET_VALUE_STR(id_0), &font3x5, 1, 0, GFX_ALIGN__LEFT, GFX_ROTATION__0);
  }
  Gfx_unsetWindow();
}


static void _setupAnimation(animation_t * animation) {
  anim_value_t* animValue = (anim_value_t*)animation->ctx;
  animValue->oldOffset = animValue->offset;
}


static void _updateAnimation(animation_t * animation, uint32_t progress) {
  anim_value_t* animValue = (anim_value_t*)animation->ctx;
  animValue->offset = ANIM_LERP(animValue->oldOffset, animValue->value, progress);
}

// static void _setupAnimation(animation_t * animation) {
//   (void)animation;
//   settings_view_t * self = &s_settingsView;
//   self->animOldOffset = self->animOffset;
// }


// static void _updateAnimation(animation_t * animation, uint32_t progress) {
//   (void)animation;
//   settings_view_t * self = &s_settingsView;
//   self->animOffset = ANIM_LERP(self->animOldOffset, self->idx * 256, progress);
// }