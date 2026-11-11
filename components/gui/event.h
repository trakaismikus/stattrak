/***************************************************************************************************
 * @file event.h
 * @brief Lorem ipsum
 **************************************************************************************************/

#ifndef _EVENT_H
#define _EVENT_H

/* > Includes *************************************************************************************/

// System
#include <stdint.h>
#include <stdbool.h>

// #include "event_config.h"


/* > Enumerations *********************************************************************************/

typedef enum event_type_s {
  EVENT_TYPE__NONE = 0,

  EVENT_TYPE__RELEASE = 0x10,
  EVENT_TYPE__PRESS,
  EVENT_TYPE__PUSH,
  EVENT_TYPE__LONG_PUSH,
  EVENT_TYPE__DOUBLE_PUSH,

} event_type_t;

/* > Macros ***************************************************************************************/

#define EVENT_IS_PRESS(_event, _btn)    (((_event)->eventId == EVENT_TYPE__PRESS) && ((_event)->sourceId == (_btn)))
#define EVENT_IS_RELEASE(_event, _btn)  (((_event)->eventId == EVENT_TYPE__RELEASE) && ((_event)->sourceId == (_btn)))

/* > Type definitions *****************************************************************************/

typedef struct event_s {
  uint8_t sourceId;
  event_type_t eventId;
} event_t;

typedef bool (*event_handle_func_t)(event_t * event, void * ctx);

typedef struct event_handler_s {
  event_handle_func_t handler;
  void * ctx;
} event_handler_t;


/* > Function Declaration *************************************************************************/

void Event_init(void);

void Event_put(event_t const * event);
bool Event_get(event_t * event);

bool Event_pushHandler(event_handle_func_t handler, void * ctx);
bool Event_popHandler(void);
bool Event_popHandlersTo(event_handle_func_t handler, bool including);
void Event_popAll(void);
void Event_consume(void);

#endif // _EVENT_H
