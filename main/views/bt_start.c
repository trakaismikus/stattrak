/***************************************************************************************************
 * @file    : bt_start.c
 * @brief   : The soft radio start of the Bluetooth screens. See bt_start.h.
 **************************************************************************************************/

#include "bt_start.h"

#include <math.h>

#include "gfx.h"
#include "time.h"

typedef enum {
  PHASE__OFF = 0,
  PHASE__WAIT,      // The view slides in, the sign shows; the radio is off
  PHASE__RADIO,     // The radio started; the sign stays
  PHASE__REVEAL,    // The screen slides down from the top
} phase_t;

static phase_t s_phase = PHASE__OFF;
static view_id_t s_view = VIEW_ID__BLANK;
static uint32_t s_time = 0;
static void (*s_start)(void) = NULL;

// The Bluetooth sign, 7 x 11
static const char* const kSign[] = {
  "...#...",
  "...##..",
  ".#.#.#.",
  "..##..#",
  "...#.#.",
  "...##..",
  "...#.#.",
  "..##..#",
  ".#.#.#.",
  "...##..",
  "...#...",
};
#define SIGN_W  7
#define SIGN_H  11

// One Bluetooth view at a time: a new begin replaces the state of the view before it (that view
// has stopped its radio in its deinit already)
void BtStart_begin(view_id_t view, void (*start)(void)) {
  s_view = view;
  s_start = start;
  s_phase = PHASE__WAIT;
  s_time = Time_getMillis();
}

void BtStart_cancel(view_id_t view) {
  if(s_view == view)
    s_phase = PHASE__OFF;
}

void BtStart_update(uint32_t now) {
  switch(s_phase) {
    case PHASE__WAIT:
      // Until the view is in (the slide of the view change has ended)
      if(AppView_get() != s_view || AppView_getNext() != s_view) {
        s_time = now;
        return;
      }
      // A few frames with only the sign on the display, then the radio starts
      if((now - s_time) < BT_START_SETTLE_MS)
        return;
      s_phase = PHASE__RADIO;
      s_time = now;
      if(s_start != NULL)
        s_start();
      break;

    case PHASE__RADIO:
      if((now - s_time) >= BT_START_SIGN_MS) {
        s_phase = PHASE__REVEAL;
        s_time = now;
      }
      break;

    case PHASE__REVEAL:
      if((now - s_time) >= BT_START_REVEAL_MS)
        s_phase = PHASE__OFF;
      break;

    default:
      break;
  }
}

static void _drawSign(uint32_t now) {
  const int16_t cx = 64, cy = 16;
  for(int16_t j = 0; j < SIGN_H; j++) {
    for(int16_t i = 0; i < SIGN_W; i++) {
      if(kSign[j][i] == '#')
        Gfx_drawPixel(cx - 3 + i, cy - 5 + j, 1);
    }
  }

  // A wave ( ) on each side, a new one each 0.3 s
  const float r = 7.0f + 9.0f * (float)(now % 300) / 300.0f;
  for(float a = -0.75f; a <= 0.75f; a += 0.06f) {
    const int16_t dx = (int16_t)lroundf(cosf(a) * r);
    const int16_t dy = (int16_t)lroundf(sinf(a) * r * 0.9f);
    Gfx_drawPixel(cx - dx, cy + dy, 1);
    Gfx_drawPixel(cx + dx, cy + dy, 1);
  }
}

bool BtStart_draw(view_id_t view, void (*screen)(void)) {
  if(s_phase == PHASE__OFF || s_view != view)
    return false;

  const uint32_t now = Time_getMillis();
  if(s_phase != PHASE__REVEAL) {
    _drawSign(now);
    return true;
  }

  // The sign moves down, the screen comes down from above (as a view change from the top)
  uint32_t t = now - s_time;
  if(t > BT_START_REVEAL_MS)
    t = BT_START_REVEAL_MS;
  const int32_t p = (int32_t)((t * 1024) / BT_START_REVEAL_MS);            // 0 .. 1024
  const int32_t eased = (p * p * (3 * 1024 - 2 * p)) >> 20;                 // smoothstep, 0 .. 1024
  const int16_t offset = (int16_t)((32 * eased) >> 10);

  gfx_ctx_t* gfx = Gfx_getContext();
  const int16_t base = gfx->offset.y;
  gfx->offset.y = base + offset;
  _drawSign(now);
  gfx->offset.y = base + offset - 32;
  if(screen != NULL)
    screen();
  gfx->offset.y = base;
  return true;
}
