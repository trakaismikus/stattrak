/*
 * Host stand-in for FreeRTOS task.h. See FreeRTOS.h in this folder.
 */
#ifndef _HOST_FREERTOS_TASK_H
#define _HOST_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;

/* Moves simulated time forward. This is the only place where time moves. */
void        vTaskDelay(TickType_t ticks);
TickType_t  xTaskGetTickCount(void);
const char* pcTaskGetName(TaskHandle_t task);

#endif // _HOST_FREERTOS_TASK_H
