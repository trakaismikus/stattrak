#ifndef _BSP_ADC_H
#define _BSP_ADC_H


void BspAdc_init(void);
void BspAdc_deinit(void);
int BspAdc_readVoltage(void);

#endif // _BSP_ADC_H