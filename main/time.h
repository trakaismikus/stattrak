#ifndef _TIME_H
#define _TIME_H


#if !defined(SIMULATOR)
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"

  #define Time_getMillis()      (pdTICKS_TO_MS(xTaskGetTickCount()))
  #define Time_delayMillis(ms)  vTaskDelay(pdMS_TO_TICKS(ms))
#else
  #include <SDL2/SDL.h>
  
  #define Time_getMillis()      (SDL_GetTicks())
  #define Time_delayMillis(ms)  SDL_Delay(ms)
#endif

#endif // _TIME_H