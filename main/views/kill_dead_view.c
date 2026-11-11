
#include "view.h"
#include "animation.h"
#include "app.h"
#include "eeprom.h"
#include "battery.h"

#include <string.h>
#include <stdio.h>


enum {
  CNT__KILL = 0,
  CNT__DEAD,
};

typedef struct {
  uint32_t value;
  int16_t yOffset;
  int16_t yOld;
  bool unfinishedAnim;
  char digitsOld[6];
  char digits[6];
  animation_t anim;
} cnt_elem_t;


static bool _eventHandler(uint16_t event);
static void _draw(void);
static void _init(void);

static void _updateCounter(uint32_t cntId);

static void _setupAnimation(animation_t * animation);
static void _teardownAnimation(animation_t * animation);
static void _updateAnimation(animation_t * animation, uint32_t progress);


view_t g_killDeadView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
  .init = _init,
};

static const animation_functions_t kAnimationFunc = {
  .setup = _setupAnimation,
  .teardown = _teardownAnimation,
  .update = _updateAnimation,
};


#define CNT_ELEM_ANIM_INIT(_ctx) {          \
    .func = &kAnimationFunc,                \
    .curve = ANIMATION_CURVE__EASE_OUT,     \
    .duration = ANIMATION_MS_TO_TICKS(300), \
    .playCount = 1,                         \
    .ctx = (_ctx),                          \
  }


static cnt_elem_t s_counters[2] = {
  { .anim = CNT_ELEM_ANIM_INIT(&s_counters[0]), .digits = "000", },
  { .anim = CNT_ELEM_ANIM_INIT(&s_counters[1]), .digits = "000", },
};





static void _init(void) {
  uint32_t kill = Eeprom_read(ENTRY_ID__GAME_KILLS);
  uint32_t dead = Eeprom_read(ENTRY_ID__GAME_DEATHS);

#ifdef ANIMATE_INITAL_VALUE
  if(s_counters[CNT__KILL].value != kill) {
    s_counters[CNT__KILL].value = kill;
    _updateCounter(CNT__KILL);
  }

  if(s_counters[CNT__DEAD].value != dead) {
    s_counters[CNT__DEAD].value = dead;
    _updateCounter(CNT__DEAD);
  }
#else
  s_counters[CNT__KILL].value = kill;
  s_counters[CNT__DEAD].value = dead;

  snprintf(s_counters[CNT__KILL].digits, sizeof(s_counters[CNT__KILL].digits), "%03lu", s_counters[CNT__KILL].value);
  snprintf(s_counters[CNT__DEAD].digits, sizeof(s_counters[CNT__DEAD].digits), "%03lu", s_counters[CNT__DEAD].value);
#endif
}


static bool _eventHandler(uint16_t event) {

  switch(event) {
    case EVENT__PRESS_1: 
      s_counters[CNT__KILL].value = Eeprom_increment(ENTRY_ID__GAME_KILLS, 0xFFFFFFFF);
      Eeprom_increment(ENTRY_ID__TOTAL_KILLS, 0xFFFFFFFF);
      _updateCounter(CNT__KILL);
      App_click(1000);
      return true;

    case EVENT__PRESS_2:
      s_counters[CNT__DEAD].value = Eeprom_increment(ENTRY_ID__GAME_DEATHS, 0xFFFFFFFF);
      Eeprom_increment(ENTRY_ID__TOTAL_DEATHS, 0xFFFFFFFF);
      _updateCounter(CNT__DEAD);
      App_click(1000);
      return true;

    case EVENT__LONG_PRESS_1:
    case EVENT__LONG_HOLD_1:
      s_counters[CNT__KILL].value = Eeprom_decrement(ENTRY_ID__GAME_KILLS, 0);
      Eeprom_decrement(ENTRY_ID__TOTAL_KILLS, 0);
      _updateCounter(CNT__KILL);
      App_click(800);
      return true;

    case EVENT__LONG_PRESS_2:
    case EVENT__LONG_HOLD_2:
      s_counters[CNT__DEAD].value = Eeprom_decrement(ENTRY_ID__GAME_DEATHS, 0);;
      Eeprom_decrement(ENTRY_ID__TOTAL_DEATHS, 0);
      _updateCounter(CNT__DEAD);
      App_click(800);
      return true;

    default: break;
  }

  return false;
}


static void _draw(void) {
  uint8_t i;
  const int16_t y = 6;
  const char* kName[2] = { "KILL", "DEAD" };

  // Draw Frame
  // Gfx_drawHLine(1,  2,  126, 1);
  // Gfx_drawHLine(1,  29, 126, 1);

/*  Gfx_drawHLine(1 ,  2,  60, 1);
  Gfx_drawHLine(67,  2,  60, 1);
  Gfx_drawHLine(1 ,  29, 60, 1);
  Gfx_drawHLine(67,  29, 60, 1);

  Gfx_drawVLine(0,  3,  26, 1);
  Gfx_drawVLine(127, 3,  26, 1);
  
  Gfx_fillRect(1,   3, 6, 26, 1);
  Gfx_fillRect(121, 3, 6, 26, 1);

  Gfx_drawPixel(7,   3,  1);
  Gfx_drawPixel(7,   28, 1);
  Gfx_drawPixel(120, 3,  1);
  Gfx_drawPixel(120, 28, 1);*/

  GUI_DRAW_ROUND_RECT(0,   2, 7, 28, 1);
  GUI_DRAW_ROUND_RECT(121, 2, 7, 28, 1);

  // Middle
  if(Eeprom_read(ENTRY_ID__SHOW_BATTERY) && Eeprom_read(ENTRY_ID__BATTERY_TYPE) > BATTERY_TYPE__USB) {
    // Gfx_fillRect(61, 2, 6, 28, 1);
    GUI_DRAW_ROUND_RECT(61, 2, 6, 28, 1);

    const uint8_t lighteningBitmap[] = { 0x04, 0x1E, 0x0F, 0x04 };
    Gfx_drawBitmapRaw(62, 3, lighteningBitmap, 4, 5, 0, GFX_ROTATION__0);

    // Battery
    Gfx_fillRect(62, 10, 4, 18, 0);
    Gfx_drawHLine(63, 9, 2, 0);
    Gfx_drawHLine(63, 28, 2, 0);

    uint16_t level = Battery_readLevel();
    uint16_t levelY = (100 * level) / 625;

    // Level
    Gfx_fillRect(63, 26, 2, -levelY, 1);  // height 0 to -16
  }
  else {
    // Gfx_drawVLine(61, 3,  26, 1);
    // Gfx_drawVLine(66, 3,  26, 1);

    Gfx_drawLineDashed(63, 1, 63, 30, 1, 2);
    Gfx_drawLineDashed(64, 2, 64, 30, 1, 2);

    // Gfx_drawLine(63, 2, 63, 30, 1);
    // Gfx_drawLine(64, 2, 64, 30, 1);
  }

  // Gfx_drawPixel(60, 3,  1);
  // Gfx_drawPixel(60, 28, 1);
  // Gfx_drawPixel(67, 3,  1);
  // Gfx_drawPixel(67, 28, 1);



  // Draw Name
  for(i=0; i<4; i++) {
    Gfx_drawChar(2      , 4 + i * 6 , kName[0][i], &font3x5, 0, 0, GFX_ROTATION__0);
    Gfx_drawChar(128 - 5, 4 + i * 6 , kName[1][i], &font3x5, 0, 0, GFX_ROTATION__0);
  }

  // Draw Digits
  // for(i=0; i<3; i++) {
  //   Gfx_drawCharScaled(10 + 17*i, y, digitsKill[i], &font5x7, 1, 3);
  //   Gfx_drawCharScaled(70 + 17*i, y, digitsDead[i], &font5x7, 1, 3);
  // }

  // Draw Counters
  const int16_t kXStarts[2] = { 10, 70 }; 
  const int16_t kXStep = 17;
  uint32_t cntId;
  for(cntId = 0; cntId < 2; cntId++) {
    cnt_elem_t* cntElem = &s_counters[cntId];
    char* digits = cntElem->digits;
    char* digitsOld = cntElem->digitsOld;
   
    if(cntElem->anim.active) {
      for(i=0; i<3; i++) {
        int16_t yOff = digits[i] != digitsOld[i] ? cntElem->yOffset : 0;
        int16_t yNew = -32;

        if(yOff != 0 && digits[i] < digitsOld[i]) {
          yOff = -yOff;
          yNew = 32;
        }

        // Old digits
        Gfx_drawCharScaled(kXStarts[cntId] + kXStep * i, y + yOff, digitsOld[i], &font5x7, 1, 3, 1);

        // New digits
        Gfx_drawCharScaled(kXStarts[cntId] + kXStep * i, y + yOff + yNew, digits[i], &font5x7, 1, 3, 1);
      }
    } 
    else {
      for(i=0; i<3; i++) {
        Gfx_drawCharScaled(kXStarts[cntId] + kXStep * i, y, digits[i], &font5x7, 1, 3, 1);
      }
    }
  }



  
  /*for(i=0; i<2; i++) {

    // Draw Digits
    for(j=0; j<3; j++) {
      Gfx_drawCharScaled(x + kx + 17*j, y, i == 0 ? digitsKill[j] : digitsDead[j], &font5x7, 1, 3);
    }

    // Draw Frame
    Gfx_drawHLine(x + 1, 2, 60, 1);
    Gfx_drawHLine(x + 1, 29, 60, 1);
    Gfx_drawVLine(x + 0, 3, 26, 1);
    Gfx_drawVLine(x + 61, 3, 26, 1);
    Gfx_fillRect(x + 1, 3, 6, 26, 1); // height was 8
    // Gfx_drawPixel(x + 7, 10, 0);
    Gfx_drawPixel(x + 60, 3, 1);
    Gfx_drawPixel(x + 60, 28, 1);
    Gfx_drawPixel(x + 1, 28, 1);

    // Draw Name
    for(j=0; j<4; j++)
      Gfx_drawChar(x + 2 , 4 + j * 6 , kName[i][j], &font3x5, 0, 0, GFX_ROTATION__0);

    x += 66;
  }*/
}

// static void _updateKills(void) {
//   cnt_elem_t* cntElem = &s_counters[CNT__KILL];
//   if(anim.active)
//   snprintf(cntElem->digits, sizeof(cntElem->digits), "%03lu", cntElem->value);
// }


// static void _updateDeaths(void) {
//   cnt_elem_t* cntElem = &s_counters[CNT__DEAD];
//   snprintf(cntElem->digits, sizeof(cntElem->digits), "%03lu", cntElem->value);
// }

static void _updateCounter(uint32_t cntId) {
  cnt_elem_t* cntElem = &s_counters[cntId];

  if(cntElem->anim.active)
    cntElem->unfinishedAnim = true;
  else {
    cntElem->unfinishedAnim = false;
    memcpy(cntElem->digitsOld, cntElem->digits, 3);
    snprintf(cntElem->digits, sizeof(cntElem->digits), "%03lu", cntElem->value);
    Animation_start(&cntElem->anim);
  }
}


static void _setupAnimation(animation_t * animation) {
  cnt_elem_t* cntElem = (cnt_elem_t*)animation->ctx;
  cntElem->yOffset = 0;
  cntElem->yOld = cntElem->yOffset;
}

static void _teardownAnimation(animation_t * animation) {
  cnt_elem_t* cntElem = (cnt_elem_t*)animation->ctx;

  if(cntElem->unfinishedAnim) {
    cntElem->unfinishedAnim = false;

    memcpy(cntElem->digitsOld, cntElem->digits, 3);
    snprintf(cntElem->digits, sizeof(cntElem->digits), "%03lu", cntElem->value);
    Animation_start(&cntElem->anim);
  }
}


static void _updateAnimation(animation_t * animation, uint32_t progress) {
  cnt_elem_t* cntElem = (cnt_elem_t*)animation->ctx;
  cntElem->yOffset = ANIM_LERP(cntElem->yOld, 32, progress);
}