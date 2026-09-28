#ifndef A02_ADC_H
#define A02_ADC_H
#include "hw.h"

int adc_open(uint32_t hz, uint32_t analog_gain /* 0..15 = 14..33 dB */, uint32_t digital_gain /* 0..59 x 0.53 dB */);
void adc_gain(uint32_t analog, uint32_t digital);
int adc_queue(void *buf, uint32_t bytes); /* 0 ok, -1 timeout */
int adc_wait_one(void);                   /* 0 ok, -1 timeout */
void adc_close(void);

#endif
