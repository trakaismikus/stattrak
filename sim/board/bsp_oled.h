#ifndef _BSP_OLED_H
#define _BSP_OLED_H

#include <stdint.h>

#define OLED_WIDTH        (128)
#define OLED_HEIGHT       (32)
#define OLED_PIXEL_SIZE   (4)


void BspOled_init(void);
void BspOled_deinit(void);


#endif // _BSP_OLED_H