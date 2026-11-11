/***************************************************************************************************
 * @file queue.h
 * @brief ...
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "queue.h"

/* > Defines **************************************************************************************/

static inline uint32_t _getNextIdx(queue_t const * p_queue, uint32_t idx);
static inline uint32_t _getUtilization(queue_t const * p_queue);

/* > Local Function Declaration *******************************************************************/

bool Queue_isFull(queue_t const * p_queue)
{
  uint32_t front  = p_queue->p_cb->front;
  uint32_t back   = p_queue->p_cb->back;

  return (_getNextIdx(p_queue, back) == front);
}

bool Queue_isEmpty(queue_t const * p_queue)
{
  uint32_t front  = p_queue->p_cb->front;
  uint32_t back   = p_queue->p_cb->back;
  return (front == back);
}

int16_t Queue_push(queue_t const * p_queue, void const * p_element)
{
  int16_t status = 0;

  CRITICAL_REGION_ENTER();

  bool is_full = Queue_isFull(p_queue);

  if (!is_full || (p_queue->mode == QUEUE_MODE__OVERFLOW)) {

    // Get write position
    uint32_t write_pos = p_queue->p_cb->back;
    p_queue->p_cb->back = _getNextIdx(p_queue, p_queue->p_cb->back);

    if (is_full) {
      // Overwrite oldest element
      p_queue->p_cb->front = _getNextIdx(p_queue, p_queue->p_cb->front);

#if defined(EVAL_QUEUE)
      p_queue->p_cb->overflow = true;
#endif
    }

    // Write new element
    switch (p_queue->element_size) {
    case sizeof(uint8_t):
      ((uint8_t *)p_queue->p_buffer)[write_pos] = *((uint8_t *)p_element);
      break;
    case sizeof(uint16_t):
      ((uint16_t *)p_queue->p_buffer)[write_pos] = *((uint16_t *)p_element);
      break;
    case sizeof(uint32_t):
      ((uint32_t *)p_queue->p_buffer)[write_pos] = *((uint32_t *)p_element);
      break;
    case sizeof(uint64_t):
      ((uint64_t *)p_queue->p_buffer)[write_pos] = *((uint64_t *)p_element);
      break;
    default:
      memcpy((void *)(p_queue->p_buffer + write_pos * p_queue->element_size),
             p_element,
             p_queue->element_size);
      break;
    }

#if defined(EVAL_QUEUE)
    // Update utilization
    uint32_t utilization = _getUtilization(p_queue);
    if(p_queue->p_cb->max_utilization < utilization)
      p_queue->p_cb->max_utilization = utilization;
#endif
  } else {
    status = 1;
  }

  CRITICAL_REGION_EXIT();

  return status;
}

int16_t Queue_genericPop(queue_t const * p_queue, void * p_element, bool just_peek)
{
  int16_t status = 0;

  CRITICAL_REGION_ENTER();

  if(!Queue_isEmpty(p_queue))
  {
    // Get read position
    uint32_t read_pos = p_queue->p_cb->front;

    // Update next read postion
    if(!just_peek)
      p_queue->p_cb->front = _getNextIdx(p_queue, p_queue->p_cb->front);

    // Read element
    switch (p_queue->element_size)
    {
    case sizeof(uint8_t):
      *((uint8_t *)p_element) = ((uint8_t *)p_queue->p_buffer)[read_pos];
      break;

    case sizeof(uint16_t):
      *((uint16_t *)p_element) = ((uint16_t *)p_queue->p_buffer)[read_pos];
      break;

    case sizeof(uint32_t):
      *((uint32_t *)p_element) = ((uint32_t *)p_queue->p_buffer)[read_pos];
      break;

    case sizeof(uint64_t):
      *((uint64_t *)p_element) = ((uint64_t *)p_queue->p_buffer)[read_pos];
      break;

    default:
      memcpy(p_element,
             (void const *)(p_queue->p_buffer + read_pos * p_queue->element_size),
             p_queue->element_size);
      break;
    }
  }
  else
  {
    status = 1; // Empty
  }

  CRITICAL_REGION_EXIT();

  return status;
}

void Queue_reset(queue_t const * p_queue)
{
  CRITICAL_REGION_ENTER();

  memset(p_queue->p_cb, 0, sizeof(queue_cb_t));

  CRITICAL_REGION_EXIT();
}

uint32_t Queue_getUtilization(queue_t const * p_queue)
{
  uint32_t utilization;

  CRITICAL_REGION_ENTER();

  utilization = _getUtilization(p_queue);

  CRITICAL_REGION_EXIT();

  return utilization;
}

uint32_t Queue_getAvilable(queue_t const * p_queue)
{
  return p_queue->size - Queue_getUtilization(p_queue);
}


#if defined(EVAL_QUEUE)

uint32_t Queue_getMaxUtilization(queue_t const * p_queue)
{
  return p_queue->p_cb->max_utilization;
}

void Queue_resetMaxUtilization(queue_t const * p_queue)
{
  p_queue->p_cb->max_utilization = 0;
}

bool Queue_hasOverflown(queue_t const * p_queue)
{
  return p_queue->p_cb->overflow;
}

#endif

/* > Local Function Definition ********************************************************************/

static inline uint32_t _getNextIdx(queue_t const * p_queue, uint32_t idx)
{
  return (idx < p_queue->size) ? (idx + 1) : 0;
}


static inline uint32_t _getUtilization(queue_t const * p_queue)
{
  uint32_t front = p_queue->p_cb->front;
  uint32_t back = p_queue->p_cb->back;

  return (back >= front) ? (back - front) : (p_queue->size + 1 - front + back);
}
