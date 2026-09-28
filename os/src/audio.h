#ifndef A02_AUDIO_H
#define A02_AUDIO_H
#include "hw.h"

void delay(uint32_t units);
void audio_init(void);
void audio_start(uint32_t hz);
int audio_rate(uint32_t hz);
void audio_volume(uint32_t vol);
int audio_busy(void);
int audio_queue(const void *pcm, uint32_t bytes); /* 16-bit LE stereo interleaved; rotate 3 buffers;
                                                   -1 = halted by the wait hook, buffer dropped */
void audio_halt(void); /* stop DMA output now */
void audio_set_wait_hook(int (*hook)(void));
extern uint32_t audio_underruns, audio_timeouts; /* timeouts: DMA never completed (halted) */
void audio_stop(void);

#endif
