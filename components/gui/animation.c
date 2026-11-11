/***************************************************************************************************
 * @file animation.c
 * @brief Lorem ipsum
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "animation.h"

/* > Defines **************************************************************************************/

#define ANIMATION__STACK_MAX_SIZE 10

/* > Enumerations *********************************************************************************/

/* > Type definitions *****************************************************************************/

/* > Local Variables ******************************************************************************/
static uint32_t m_tickCounter = 0;

static animation_t * m_stack[ANIMATION__STACK_MAX_SIZE] = { NULL, };

// TODO: Should implement doubly linked list for this instead
static uint8_t m_stackSize = 0;


/* > Local Function Declaration *******************************************************************/
static void _addToStack(animation_t * animation);
static void _removeFromStack(animation_t * animation);
static void _updateAnimations(void);
static void _completeAnimation(animation_t * animation);

/* > Global Function Definition *******************************************************************/

void Animation_create(animation_t * animation)
{
  animation->startDelay = 0;
  animation->endDelay = 0;
  animation->active = false;
  animation->playCount = 1;
  animation->playCounter = 0;
}

void Animation_start(animation_t * animation)
{
  animation->startTime = m_tickCounter;
  animation->playCounter = 0;
  _addToStack(animation);
}

void Animation_stop(animation_t * animation)
{
  _removeFromStack(animation);
}

void Animation_runTickHandler(uint32_t ticks)
{
  m_tickCounter = ticks;
  _updateAnimations();
}

void Animation_setTickCount(uint32_t ticks) {
  m_tickCounter = ticks;
}

uint32_t Animation_getTickCount(void)
{
  return m_tickCounter;
}

/* > Local Function Definition ********************************************************************/

static void _addToStack(animation_t * animation)
{
  // If active, restart without teardown
  if (animation->active)
  {
    if (animation->func->teardown)
    {
      animation->func->teardown(animation);
    }
    if (animation->func->setup)
    {
      animation->func->setup(animation);
    }
    return;
  }

  for (uint8_t i = 0; i < ANIMATION__STACK_MAX_SIZE; ++i)
  {
    if (m_stack[i] == NULL)
    {
      animation->active = true;
      if (animation->func->setup)
      {
        animation->func->setup(animation);
      }
      m_stack[i] = animation;
      m_stackSize++;
      return;
    }
  }
}

static void _removeFromStack(animation_t * animation)
{
  for (uint8_t i = 0; i < ANIMATION__STACK_MAX_SIZE; ++i)
  {
    if (m_stack[i] == animation)
    {
      animation->active = false;
      if (animation->func->teardown)
      {
        animation->func->teardown(animation);
      }
      m_stack[i] = NULL;
      m_stackSize--;
      return;
    }
  }
}

static void _updateAnimations(void)
{
  uint8_t updatedAnimationCount = 0;
  uint8_t animationsToUpdate = m_stackSize;
  for (uint8_t i = 0; i < ANIMATION__STACK_MAX_SIZE; ++i)
  {
    if (m_stack[i] != NULL)
    {
      animation_t * animation = m_stack[i];
      uint32_t now = m_tickCounter;
      if (now < (animation->startTime + animation->startDelay))
      {
        continue;
      }
      uint32_t progress = now - (animation->startTime + animation->startDelay);

      if (progress < animation->duration)
      {
        progress *= ANIMATION__NORMALIZED_MAX;
        progress /= animation->duration;

        switch(animation->curve) {
        case ANIMATION_CURVE__DEFAULT:
        case ANIMATION_CURVE__LINEAR:
          break;
        case ANIMATION_CURVE__EASE_IN:
          progress = ((progress * progress) / ANIMATION__NORMALIZED_MAX);
          break;
        case ANIMATION_CURVE__EASE_OUT:
          if(progress == 0)
            progress = 0;
          else {
            progress -= ANIMATION__NORMALIZED_MAX;
            progress = -(progress * progress / ANIMATION__NORMALIZED_MAX) + ANIMATION__NORMALIZED_MAX;
          }
          break;
        case ANIMATION_CURVE__EASE_IN_OUT:
          if (progress < ANIMATION__NORMALIZED_MAX / 2){
            progress = ((progress * progress) / (ANIMATION__NORMALIZED_MAX / 2));
          }
          else {
            progress -= ANIMATION__NORMALIZED_MAX;
            progress = -((progress * progress) / (ANIMATION__NORMALIZED_MAX / 2)) + ANIMATION__NORMALIZED_MAX;
          }
          break;
        case ANIMATION_CURVE__CUSTOM:
          //if (animation->curve_function)
            //progress = animation->curve_function(progress);
          break;
        default:
          break;
        }

        animation->func->update(animation, progress);
      }
      else
      {
        animation->func->update(animation, ANIMATION__NORMALIZED_MAX);
        if (progress > (animation->duration + animation->endDelay))
        {
          _completeAnimation(animation);
        }
      }

      // Check if all animations updated
      updatedAnimationCount++;
      if (updatedAnimationCount == animationsToUpdate)
      {
        break;
      }
    }
  }
}

static void _completeAnimation(animation_t * animation)
{
  animation->playCounter++;

  if (animation->playCount == ANIMATION__PLAY_COUNT_INFINITE ||
      animation->playCount < animation->playCounter)
  {
    if (animation->func->teardown)
    {
      animation->func->teardown(animation);
    }

    animation->startTime = m_tickCounter;

    if (animation->func->setup)
    {
      animation->func->setup(animation);
    }
  }
  else
  {
    _removeFromStack(animation);
  }
}
