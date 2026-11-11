#ifndef _BSP_BUTTON_H
#define _BSP_BUTTON_H

#include <stdint.h>
#include <SDL2/SDL.h>


void BspButton_init(void);
void BspButton_deinit(void);
uint8_t BspButton_getEvent(void);

void BspButton_process(SDL_Event* event);

#endif // _BSP_BUTTON_H