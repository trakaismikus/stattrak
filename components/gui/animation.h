/***************************************************************************************************
 * @file animation.h
 * @brief Lorem ipsum
 **************************************************************************************************/

#ifndef _ANIMATION_H
#define _ANIMATION_H

/* > Includes *************************************************************************************/
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* > Defines **************************************************************************************/

#define ANIMATION__DURATION_INFINITE UINT32_MAX
#define ANIMATION__NORMALIZED_MIN 0
#define ANIMATION__NORMALIZED_MAX 65536

#define ANIMATION__PLAY_COUNT_INFINITE UINT32_MAX

/* Use to easily calculate changes */
#define ANIM_LERP(a, b, progress)  ((a) + ((b) - (a)) * progress / ANIMATION__NORMALIZED_MAX)

#define ANIMATION__TIMER_FREQ_HZ 1000
#define ANIMATION__TIMER_PERIOD (1000 / ANIMATION__TIMER_FREQ_HZ)

#define ANIMATION_MS_TO_TICKS(value) (value / ANIMATION__TIMER_PERIOD)

/* > Enumerations *********************************************************************************/

typedef enum animation_curve_s {
  ANIMATION_CURVE__DEFAULT,
  ANIMATION_CURVE__LINEAR,
  ANIMATION_CURVE__EASE_IN,
  ANIMATION_CURVE__EASE_OUT,
  ANIMATION_CURVE__EASE_IN_OUT,
  ANIMATION_CURVE__CUSTOM,
} animation_curve_t;

/* > Type definitions *****************************************************************************/
typedef uint32_t animation_tick_t;

typedef struct animation_s animation_t;

typedef void (*animation_setup_func_t)(animation_t * animation);
typedef void (*animation_update_func_t)(animation_t * animation, uint32_t progress);
typedef void (*animation_teardown_func_t)(animation_t * animation);

typedef struct animation_functions_s {
  animation_setup_func_t setup;
  animation_update_func_t update;
  animation_teardown_func_t teardown;
} animation_functions_t;

struct animation_s {
  uint32_t playCount;
  uint32_t playCounter;
  animation_tick_t startTime;
  animation_tick_t duration;
  animation_tick_t startDelay;
  animation_tick_t endDelay;
  void * ctx;
  const animation_functions_t * func;
  animation_curve_t curve;
  bool active;
};

/* > Function Declaration *************************************************************************/

void Animation_create(animation_t * animation);

void Animation_start(animation_t * animation);
void Animation_stop(animation_t * animation);

void Animation_runTickHandler(uint32_t ticks);
void Animation_setTickCount(uint32_t ticks);
uint32_t Animation_getTickCount(void);

#endif // _ANIMATION_H
