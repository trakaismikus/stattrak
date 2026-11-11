#ifndef _BSP_BUTTON_H
#define _BSP_BUTTON_H

#include <stdint.h>

void BspButton_init(void);
void BspButton_deinit(void);
void BspButton_clearEvents(void);
uint8_t BspButton_getEvent(void);
uint8_t BspButton_getState(uint8_t btnId);

#endif // _BSP_BUTTON_H