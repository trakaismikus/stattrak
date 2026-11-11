/***************************************************************************************************
 * @file event.c
 * @brief Lorem ipsum
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "event.h"
#include "queue.h"
// #include "debug.h"

/* > Defines **************************************************************************************/

#ifndef EVENT__FIFO_SIZE
  #define EVENT__FIFO_SIZE 64
#endif

#ifndef EVENT__HANDLER_STACK
  #define EVENT__HANDLER_STACK 4
#endif

#define EVENT_PRINTF(format, ...)   DBG(format, ##__VA_ARGS__)
// #define EVENT_PRINTF(format, ...)   (void)0

// #define EVENT_DEBUG_PRINTF(format, ...)   debug_printf(format, ##__VA_ARGS__)
#define EVENT_DEBUG_PRINTF(format, ...)     (void)0

/* > Enumerations *********************************************************************************/

/* > Type definitions *****************************************************************************/

/* > Local Variables ******************************************************************************/

static event_handler_t s_eventHandlers[EVENT__HANDLER_STACK];
static uint8_t s_eventHandlerCount = 0;

QUEUE_DEF(event_t, s_fifo, EVENT__FIFO_SIZE, QUEUE_MODE__OVERFLOW);

/* > Local Function Declaration *******************************************************************/

/* > Global Function Definition *******************************************************************/

void Event_init(void)
{
  Queue_reset(&s_fifo);
}

void Event_put(event_t const * event)
{
  (void)Queue_push(&s_fifo, (void *)event);

  // if(event->eventId == EVENT_TYPE__ENCODER) {
  //   int8_t diff = 0;
  //   if(event->sourceId == EVENT_SRC__ENC_DATA)
  //     diff = Encoder_getDiff(ENC_DATA);
  //   else
  //     diff = Encoder_getDiff(ENC(event->sourceId));

  //   EVENT_PRINTF("%c%c%c%c", 0x80, event->eventId, event->sourceId, diff);
  // } else {
  //   EVENT_PRINTF("%c%c%c%c", 0x80, event->eventId, event->sourceId, 0x00);
  // }
}

bool Event_get(event_t * event)
{
  return Queue_pop(&s_fifo, (void *)event) ? false : true;
}

bool Event_pushHandler(event_handle_func_t handler, void * ctx)
{
  EVENT_DEBUG_PRINTF("PUSH %p\n", handler);
  if (s_eventHandlerCount < EVENT__HANDLER_STACK)
  {
    s_eventHandlers[s_eventHandlerCount].handler = handler;
    s_eventHandlers[s_eventHandlerCount].ctx = ctx;
    s_eventHandlerCount++;
    return true;
  }

  return false;
}

bool Event_popHandler(void)
{
  if (s_eventHandlerCount > 0)
  {
    EVENT_DEBUG_PRINTF("POP %p\n", s_eventHandlers[s_eventHandlerCount - 1].handler);
    s_eventHandlerCount--;
    return true;
  }

  return false;
}

bool Event_popHandlersTo(event_handle_func_t handler, bool including)
{
  while (handler != s_eventHandlers[s_eventHandlerCount - 1].handler)
  {
    if (!Event_popHandler())
    {
      return false;
    }
  }

  if (including)
  {
    Event_popHandler();
  }

  return true;
}

void Event_popAll(void)
{
  s_eventHandlerCount = 0;
}

void Event_consume(void)
{
  event_t event;
  if (Event_get(&event))
  {
    // Feed the event to the handlers in the stack
    for (uint8_t i = s_eventHandlerCount; i > 0; i--)
    {
      const uint8_t idx = i - 1;
      if (s_eventHandlers[idx].handler(&event, s_eventHandlers[idx].ctx))
      {
        // Event consumed
        return;
      }
    }
  }
}
/* > Local Function Definition ********************************************************************/
