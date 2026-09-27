/* Audio output for A02-OS: stock-equivalent power-up + anti-pop ramp, then DMA streaming of
 * 16-bit interleaved stereo PCM to the DAC FIFO (settings from stock aud_dev.drv). */
#include "audio.h"

#define DAC_BASE   0xc0180000
#define DAC(o)     REG(DAC_BASE + (o))
#define PMU_BASE   0xc0010000
#define DMA_PEND   REG(0xc0070000)
#define ADMA       2 /* DMA channel used by stock driver for the DAC */
#define DMAC(o)    REG(0xc0070100 + ADMA * 0x100 + (o))

static void fifo_put(int32_t s)
{
	while (!(DAC(0x08) & 0x80))
		;
	DAC(0x0c) = (uint32_t)s;
	DAC(0x08) = 0x80;
}

int audio_rate(uint32_t hz)
{
	static const uint8_t divs[8] = { 1, 2, 3, 4, 6, 8, 12, 0 };
	uint32_t khz = hz / 1000;
	uint32_t fam44 = (hz % 11025) == 0;
	uint32_t d = (fam44 ? 176 : 192) / (fam44 ? hz / 11025 * 11 : khz), half = 0, i;
	if (d == 16 || d == 24) {
		d >>= 1;
		half = 1;
	}
	for (i = 0; i < 8 && divs[i] != d; i++)
		;
	if (i == 8)
		return -1;
	REG(CMU_BASE + 0x88) = (REG(CMU_BASE + 0x88) & 0xffffffa8u) | fam44 << 6 | half << 4 | i;
	return 0;
}

static void audio_power(void)
{
	REG(PMU_BASE + 0x18) = (REG(PMU_BASE + 0x18) & ~0x20u) | 0x40;
	for (volatile int i = 0; i < 0x14; i++)
		;
	REG(0xc0000004) = (REG(0xc0000004) & ~4u) | 4;
	REG(0xc0000118) = (REG(0xc0000118) & ~0x1fu) | 0x1c;
	REG(0xc000011c) = (REG(0xc000011c) & ~0x1fu) | 0x14;
	delay(20);
}

static void pa_ramp_ac(void)
{
	int32_t v = 0x7ffff;
	int n = 30000;
	static const struct { int until, step; } seg[] = {
		{ 25000, 50 }, { 20000, 45 }, { 15000, 38 }, { 10000, 30 }, { 7000, 26 }, { 5000, 20 }, { 0, 15 },
	};

	audio_rate(48000);
	DAC(0x28) |= 0x80;
	DAC(0x28) &= ~0x300u;
	DAC(0x2c) |= 0x808;
	DAC(0x28) |= 3;
	DAC(0x28) |= 0x14;
	DAC(0x00) |= 1;
	DAC(0x00) |= 0x100;
	DAC(0x00) |= 0x10;
	DAC(0x04) = (DAC(0x04) & 0xffffffc9u) | 1;
	for (int i = 0; i < 0xc0; i++)
		fifo_put(v << 12);
	DAC(0x2c) |= 0x101;
	DAC(0x2c) |= 0x202;
	for (unsigned s = 0; s < sizeof(seg) / sizeof(seg[0]); s++) {
		for (; n > seg[s].until; n--) {
			fifo_put(v << 12);
			v -= seg[s].step;
		}
		wdt_feed();
	}
	for (int i = 0; i < 0x960; i++)
		fifo_put(v << 12);
	DAC(0x28) |= 8;
	DAC(0x28) |= 0x20;
	for (int i = 0; i < 0xc0; i++)
		fifo_put(v << 12);
	DAC(0x2c) &= 0xfffffefeu;
	DAC(0x2c) &= 0xfffffdfdu;
	for (int i = 0; i < 0x60; i++)
		fifo_put(0);
	REG(CMU_BASE + 0x0c) |= 0x800;
	DAC(0x28) &= 0xfffcffffu;
	DAC(0x28) |= 0x10000;
	DAC(0x28) |= 0x40000;
	DAC(0x28) |= 0x300;
	DAC(0x04) &= ~1u;
}

void audio_volume(uint32_t vol)
{
	DAC(0x14) = (DAC(0x14) & 0xfff00f00u) | 0x33700 | (vol & 0xff);
	DAC(0x18) = (DAC(0x18) & 0xfff00f00u) | 0x33700 | (vol & 0xff);
}

void audio_init(void)
{
	audio_power();
	REG(CMU_BASE + 0x0c) |= 4;
	REG(CMU_BASE + 0x0c) |= 0x800;
	REG(CMU_BASE + 0x88) |= 0x400;
	DAC(0x28) = (DAC(0x28) & 0xfffc8fffu) | 0x8000080 | 1u << 10;
	pa_ramp_ac();
	DAC(0x28) |= 7u << 12;
	DAC(0x2c) |= 0x808;
	audio_volume(0xa0);
}

/* FUN_0d8007f2(0, rate, ch=2, 1, 0): DAC in DMA mode */
void audio_start(uint32_t hz)
{
	REG(CMU_BASE + 0x0c) |= 4;
	audio_rate(hz);
	REG(CMU_BASE + 0x88) |= 0x400;
	DAC(0x00) = (DAC(0x00) & ~0xcu) | 0x100 | 2u << 2 | 3;
	DAC(0x30) = (DAC(0x30) & 0xf8ff8fffu) | 0x6004000;
	/* FUN_0d80055c(1) */
	DAC(0x04) &= ~0x11u;
	DAC(0x04) |= 1;
	DAC(0x00) |= 0x10;
	DAC(0x0c) = 0;
	DAC(0x04) |= 0x12;
	DAC(0x04) |= 1u << 7;
	/* FUN_0d80059c(ch, 1, 0) */
	DMAC(4) &= ~1u;
	DMAC(0x00) = 1u << 20 | 0x40000 | 0x8b00;
	DMAC(0x10) = DAC_BASE + 0x0c;
	DMAC(0x08) = 0;
	DMAC(0x0c) = 0;
}

/* Reload-mode streaming. In reload mode the channel restarts from its registers as soon as a
 * transfer completes (flag 0xc0070000 bit ch), so the NEXT buffer must already be programmed
 * before the current one ends (queue-ahead). audio_queue() programs the new buffer as "next";
 * if one is already pending it first waits for the playing buffer to complete (the pending one
 * then becomes the playing one). Callers rotate 3 buffers: when audio_queue() returns, the buffer
 * queued two calls earlier has finished playing and may be reused. */
static int running, pending;
uint32_t audio_underruns;

int audio_busy(void)
{
	return running;
}

static void dma_set(const void *pcm, uint32_t bytes)
{
	DMAC(0x08) = (uint32_t)pcm;
	DMAC(0x0c) = 0;
	DMAC(0x18) = bytes;
}

static void wait_complete(void)
{
	for (uint32_t i = 0; !(DMA_PEND & (1u << ADMA)); i++)
		if ((i & 0xfff) == 0)
			wdt_feed();
	DMA_PEND = 1u << ADMA;
}

void audio_queue(const void *pcm, uint32_t bytes)
{
	if (!running) {
		DMA_PEND = 1u << ADMA;
		dma_set(pcm, bytes);
		DMAC(4) |= 1;
		running = 1;
		pending = 0;
		return;
	}
	if (pending) {
		/* already complete when we got here = the pending buffer started late or replayed */
		if (DMA_PEND & (1u << ADMA))
			audio_underruns++;
		wait_complete();
	}
	dma_set(pcm, bytes);
	pending = 1;
}

void audio_stop(void)
{
	if (running) {
		if (pending)
			wait_complete();
		wait_complete();
	}
	DMAC(4) &= ~1u;
	DMA_PEND = 1u << ADMA;
	running = pending = 0;
}
