
#include "view.h"
#include "animation.h"
#include "app.h"
#include "eeprom.h"
#include "battery.h"
#include "app_host_view.h"

#include <string.h>
#include <stdio.h>


static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

static void _updateTotal(void);

static void _setupAnimation(animation_t * animation);
static void _teardownAnimation(animation_t * animation);
static void _updateAnimation(animation_t * animation, uint32_t progress);

view_t g_totalView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};

static const animation_functions_t kAnimationFunc = {
  .setup = _setupAnimation,
  .update = _updateAnimation,
  .teardown = _teardownAnimation,
};

static uint32_t s_total = 0;
static char digits[6] = "00000", digitsOld[6] = "00000";
static int16_t yOffset = 0, yOld = 0;
static bool unfinishedAnim = false;
static uint32_t s_id = ENTRY_ID__TOTAL_KILLS;

static animation_t anim = {
  .func = &kAnimationFunc,
  .curve = ANIMATION_CURVE__EASE_OUT,
  .duration = ANIMATION_MS_TO_TICKS(300),
  .playCount = 1,
};


static void _init(void) {

  s_id = (Eeprom_read(ENTRY_ID__COUNTER) == COUNTER__TOTAL) 
    ? ENTRY_ID__TOTAL_KILLS : ENTRY_ID__GAME_KILLS;
  


#ifdef ANIMATE_INITAL_VALUE
  if(total != s_total) {
    s_total = total;
   _updateTotal();
  }
#else
  s_total = Eeprom_read(s_id);
  snprintf(digits, sizeof(digits), "%05lu", s_total);
#endif
}


static bool _eventHandler(uint16_t event) {

  switch(event) {
    case EVENT__PRESS_1: 
    case EVENT__PRESS_2: {
      Eeprom_increment(ENTRY_ID__TOTAL_KILLS, 0xFFFFFFFF);
      Eeprom_increment(ENTRY_ID__GAME_KILLS, 0xFFFFFFFF);
      s_total = Eeprom_read(s_id);
      _updateTotal();
      AppHost_counterAdded(APP_HOST_COUNTER__KILLS);   // The background app (docs/APPS.md 4.6)
      App_click(1000);
    } return true;

    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_1:
    case EVENT__LONG_HOLD_2: {
      Eeprom_decrement(ENTRY_ID__TOTAL_KILLS, 0); 
      Eeprom_decrement(ENTRY_ID__GAME_KILLS, 0);
      s_total = Eeprom_read(s_id);
      _updateTotal();
      App_click(800);
    } return true;

    default: break;
  }

  return false;
}


static void _draw(void) {
  uint32_t i;
  int16_t x = 6;
  int16_t xStep = 24;


  if(Eeprom_read(ENTRY_ID__SHOW_BATTERY) && Eeprom_read(ENTRY_ID__BATTERY_TYPE) > BATTERY_TYPE__USB) {
    x = 4;
    xStep = 22;

    uint16_t level = Battery_readLevel();
    const uint8_t kTriangleBmp[] = { 4,6,7 };

    uint16_t levelIdx = (100 * level) / 357;
    uint8_t row = levelIdx >> 2;
    uint8_t col = levelIdx & 3;

    // Gfx_drawChar(119-5, 0, row + '0', &font3x5, 1, 0, GFX_ROTATION__0);
    // Gfx_drawChar(119-5, 7, col + '0', &font3x5, 1, 0, GFX_ROTATION__0);

    // Show Battery
    for(i=0; i<7; i++) {
      
      if(row == i) {
        if(col == 3)
          Gfx_fillRect(119 + 0, 26 - i*4, 3, 3, 1);
        else if(col == 2)
          Gfx_drawBitmapRaw(119 + 0, 26 - i*4, kTriangleBmp, 3, 3, 1, GFX_ROTATION__0);
          

        if(col >= 1)
          Gfx_fillRect(119 + 4, 26 - i*4, 3, 3, 1);
        else if(col == 0)
          Gfx_drawBitmapRaw(119 + 4, 26 - i*4, kTriangleBmp, 3, 3, 1, GFX_ROTATION__0);

      } else if(row > i) {
        Gfx_fillRect(119 + 0, 26 - i*4, 3, 3, 1);
        Gfx_fillRect(119 + 4, 26 - i*4, 3, 3, 1); 
      }
      
    }
  }


  if(anim.active) {
    for(i=0; i<5; i++) {
      int16_t yOff = digits[i] != digitsOld[i] ? yOffset : 0;
      int16_t yNew = -32;

      if(yOff != 0 && digits[i] < digitsOld[i]) {
        yOff = -yOff;
        yNew = 32;
      }

      // Old digits
      Gfx_drawCharScaled(x + xStep * i, 2 + yOff, digitsOld[i], &font5x7, 1, 4, 1);

      // New digits
      Gfx_drawCharScaled(x + xStep * i, 2 + yOff + yNew, digits[i], &font5x7, 1, 4, 1);
    }
  } 
  else {
    for(i=0; i<5; i++) {
      Gfx_drawCharScaled(x + xStep * i, 2, digits[i], &font5x7, 1, 4, 1);
    }
  }
}


static void _updateTotal(void) {
  if(anim.active)
    unfinishedAnim = true;
  else {
    unfinishedAnim = false;
    memcpy(digitsOld, digits, 5);
    snprintf(digits, sizeof(digits), "%05lu", s_total);
    Animation_start(&anim);
  }
}


static void _setupAnimation(animation_t * animation) {
  (void)animation;
  yOffset = 0;
  yOld = yOffset;
}

static void _teardownAnimation(animation_t * animation) {
  if(unfinishedAnim)
    _updateTotal();
}


static void _updateAnimation(animation_t * animation, uint32_t progress) {
  (void)animation;
  yOffset = ANIM_LERP(yOld, 32, progress);
}