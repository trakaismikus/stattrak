#ifndef _BSP_BUZZER_H
#define _BSP_BUZZER_H

#include <stdint.h>

void BspBuzzer_init(void);
void BspBuzzer_deinit(void);

void Buzzer_click(uint16_t freq);
void Buzzer_beep(uint16_t freq, uint8_t duty);
void Buzzer_off(void);

#endif // _BSP_BUZZER_H