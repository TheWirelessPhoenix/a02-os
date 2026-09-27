#ifndef A02_AUDIO_H
#define A02_AUDIO_H
#include "hw.h"

void delay(uint32_t units);
void audio_init(void);
void audio_start(uint32_t hz);
int audio_rate(uint32_t hz);
void audio_volume(uint32_t vol);
int audio_busy(void);
void audio_queue(const void *pcm, uint32_t bytes); /* 16-bit LE stereo interleaved; rotate 3 buffers */
extern uint32_t audio_underruns;
void audio_stop(void);

#endif
