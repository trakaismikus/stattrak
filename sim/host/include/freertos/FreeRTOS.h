/*
 * Host stand-in for FreeRTOS.h.
 *
 * There is no scheduler here. Time is simulated: it only moves when the
 * firmware calls vTaskDelay(). The tick rate is 100 Hz, the same as the
 * ESP-IDF default that the device uses, so Time_getMillis() has the same
 * 10 ms resolution as on the device.
 */
#ifndef _HOST_FREERTOS_H
#define _HOST_FREERTOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t TickType_t;
typedef int      BaseType_t;

#define configTICK_RATE_HZ    (100)
#define portTICK_PERIOD_MS    ((TickType_t)(1000 / configTICK_RATE_HZ))
#define portMAX_DELAY         ((TickType_t)0xFFFFFFFFUL)

#define pdFALSE               (0)
#define pdTRUE                (1)

#define pdMS_TO_TICKS(ms)     ((TickType_t)(((uint64_t)(ms) * (uint64_t)configTICK_RATE_HZ) / (uint64_t)1000U))
#define pdTICKS_TO_MS(ticks)  ((TickType_t)(((uint64_t)(ticks) * (uint64_t)1000U) / (uint64_t)configTICK_RATE_HZ))

#endif // _HOST_FREERTOS_H
