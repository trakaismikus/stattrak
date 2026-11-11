/***************************************************************************************************
 * @file    : space_view.c
 * @brief   : Space Screensaver View source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "view.h"
#include "app.h"
#include "gfx.h"

#include <stdlib.h>

/* > Local Function Declarations ******************************************************************/
static bool _eventHandler(uint16_t event);
static void _draw(void);

/* > Defines **************************************************************************************/

/* > Macros ***************************************************************************************/

#define elemof(_x)  ((sizeof(_x)/sizeof((_x)[0])))

/* > Type Definitions *****************************************************************************/
typedef struct star_s {
  int32_t x, y;
  int32_t sx, sy;
  uint8_t age;
  bool alive;
} star_t;

static void Star_init(star_t* star);
static void Star_draw(star_t* star);


/* > Enumerations *********************************************************************************/

/* > Constants ************************************************************************************/

static const int32_t kStarAcceleration = 1000;

/* > Local Variables ******************************************************************************/
static star_t stars[32];

/* > Global Variables ******************************************************************************/
view_t g_spaceView = {
  .eventHandler = _eventHandler,
  .draw = _draw,
};


/* > Local Function Definitions *******************************************************************/

static bool _eventHandler(uint16_t event) {

  switch(event) {
    
    case EVENT__PRESS_1: 
      return true;

    case EVENT__PRESS_2:
      return true;

    case EVENT__LONG_PRESS_1:
      return true;

    case EVENT__LONG_PRESS_2:
      return true;

    default: break;
  }

  return false;
}


static void _draw(void) {
  uint8_t i;

  for(i=0; i<elemof(stars); i++) {
    star_t* star = &stars[i];

    if(!star->alive)
      Star_init(star);

    Star_draw(star);
  }
}



static void Star_init(star_t* self) {
  self->x = (128 / 2) << 16;
  self->y = (32 / 2) << 16;

  self->sx = (rand() - RAND_MAX/2) >> 13;
  self->sy = (rand() - RAND_MAX/2) >> 13;

  self->x += self->sx * 128 / 20;
  self->y += self->sy * 128 / 20;

  self->age = 0;
  self->alive = true;
}

static void Star_draw(star_t* self) {

  uint8_t age = self->age >> 4;

  int16_t x = self->x >> 16;
  int16_t y = self->y >> 16;

  if(age == 0)
    Gfx_drawPixel(x, y, 1);
  else if(age == 1) {
    Gfx_drawPixel(x, y, 1);
    Gfx_drawPixel(x + 1, y, 1);
    Gfx_drawPixel(x - 1, y, 1);
    Gfx_drawPixel(x, y + 1, 1);
    Gfx_drawPixel(x, y - 1, 1);
  } else if(age == 2) {
    Gfx_drawPixel(x, y, 1);
    Gfx_drawPixel(x + 1, y, 1);
    Gfx_drawPixel(x + 1, y + 1, 1);
    Gfx_drawPixel(x + 1, y - 1, 1);
    Gfx_drawPixel(x - 1, y, 1);
    Gfx_drawPixel(x - 1, y + 1, 1);
    Gfx_drawPixel(x - 1, y - 1, 1);
    Gfx_drawPixel(x, y + 1, 1);
    Gfx_drawPixel(x, y - 1, 1);
  } else if(age >= 3) {
    Gfx_drawPixel(x, y, 1);
    Gfx_drawPixel(x + 1, y, 1);
    Gfx_drawPixel(x + 1, y + 1, 1);
    Gfx_drawPixel(x + 1, y - 1, 1);
    Gfx_drawPixel(x - 1, y, 1);
    Gfx_drawPixel(x - 1, y + 1, 1);
    Gfx_drawPixel(x - 1, y - 1, 1);
    Gfx_drawPixel(x, y + 1, 1);
    Gfx_drawPixel(x, y - 1, 1);

    Gfx_drawPixel(x + 2, y, 1);
    Gfx_drawPixel(x - 2, y, 1);
    Gfx_drawPixel(x, y + 2, 1);
    Gfx_drawPixel(x, y - 2, 1);
  }

  self->age++;

  self->x += self->sx;
  self->y += self->sy;

  self->sx += (self->sx * kStarAcceleration) >> 16;
  self->sy += (self->sy * kStarAcceleration) >> 16;

  if(x < 0 || y < 0 || x > Gfx_getWidth() || y > Gfx_getHeight())
    self->alive = false;
}