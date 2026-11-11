/***************************************************************************************************
 * @file queue.h
 * @brief ...
 **************************************************************************************************/

#ifndef _QUEUE_H
#define _QUEUE_H


#include <stdint.h>
#include <stdbool.h>
#include <string.h>


//#define EVAL_QUEUE
//#define QUEUE_CRITICAL

#if defined( QUEUE_CRITICAL)
#define CRITICAL_REGION_ENTER() app_util_critical_region_enter()
#define CRITICAL_REGION_EXIT() app_util_critical_region_exit()
#else
#define CRITICAL_REGION_ENTER()
#define CRITICAL_REGION_EXIT()
#endif


#define QUEUE_DEF(_type, _name, _size, _mode)               \
  static _type        _name ## _queue_buffer[(_size) + 1];  \
  static queue_cb_t   _name ## _queue_cb;                   \
  const queue_t       _name =                               \
  {                                                         \
    .p_cb         = &_name ## _queue_cb,                    \
    .p_buffer     = _name ## _queue_buffer,                 \
    .size         = (_size),                                \
    .element_size = sizeof(_type),                          \
    .mode         = _mode,                                  \
  }


typedef enum queue_mode_e
{
  QUEUE_MODE__OVERFLOW,        // If the queue is full, new element will overwrite the oldest.
  QUEUE_MODE__NO_OVERFLOW,     // If the queue is full, new element will not be accepted.
} queue_mode_t;


// Queue Control block
typedef struct
{
  volatile uint32_t      front;            // Queue front index
  volatile uint32_t      back;             // Queue back index

#if defined(EVAL_QUEUE)
  uint32_t      max_utilization;  // Maximum utilization of the queue
  bool          overflow;
#endif
} queue_cb_t;

typedef struct
{
  queue_cb_t   *p_cb;             // Queue control block
  void         *p_buffer;         // Pointer to the memory that is used as storage
  uint32_t      size;             // Size of the queue
  uint32_t      element_size;     // Size of one element
  queue_mode_t  mode;             // Queue mode
} queue_t;



int16_t   Queue_genericPop(queue_t const * p_queue, void * p_element, bool just_peek);
int16_t   Queue_push(queue_t const * p_queue, void const * p_element);

#define   Queue_pop(_p_queue, _p_element)     Queue_genericPop((_p_queue), (_p_element), false)
#define   Queue_peek(_p_queue, _p_element)    Queue_genericPop((_p_queue), (_p_element), true)

bool      Queue_isFull(queue_t const * p_queue);
bool      Queue_isEmpty(queue_t const * p_queue);
uint32_t  Queue_getUtilization(queue_t const * p_queue);
uint32_t  Queue_getAvilable(queue_t const * p_queue);
void      Queue_reset(queue_t const * p_queue);


#if defined(EVAL_QUEUE)
uint32_t  Queue_getMaxUtilization(queue_t const * p_queue);
void      Queue_resetMaxUtilization(queue_t const * p_queue);
bool      Queue_hasOverflown(queue_t const * p_queue);
#endif


#endif /* _QUEUE_H */
