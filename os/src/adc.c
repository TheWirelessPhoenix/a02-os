/* Microphone input (ADC 0xc0181000), equivalent to stock aud_dev.drv record open (FUN_0d8206b2,
 * mono mic path: input 4, left channel) streamed by DMA from the ADC FIFO in reload mode. */
#include "adc.h"
#include "lcd.h" /* delay() */

#define ADC(o)     REG(0xc0181000 + (o))
#define PMU_BASE   0xc0010000
#define DMA_PEND   REG(0xc0070000)
#define RDMA       3 /* our choice: 2 = DAC, 4 = SD */
#define DMAC(o)    REG(0xc0070100 + RDMA * 0x100 + (o))

static int running, pending;

static int adc_rate(uint32_t hz)
{
	static const uint8_t divs[8] = { 1, 2, 3, 4, 6, 8, 12, 0 };
	uint32_t fam44 = (hz % 11025) == 0;
	uint32_t khz = fam44 ? hz / 11025 * 11 : hz / 1000;
	uint32_t d = (fam44 ? 176 : 192) / khz, half = 0, i;
	if (d == 16 || d == 24) {
		d >>= 1;
		half = 1;
	}
	for (i = 0; i < 8 && divs[i] != d; i++)
		;
	if (i == 8)
		return -1;
	uint32_t v = REG(CMU_BASE + 0x80) & 0xfffffc38u;
	REG(CMU_BASE + 0x80) = v | fam44 << 6 | half << 4 | i | 0x33000000;
	return 0;
}

void adc_gain(uint32_t analog, uint32_t digital)
{
	if (analog > 15)
		analog = 15;
	if (digital > 59)
		digital = 59;
	ADC(0x44) = (ADC(0x44) & 0xfffff0ffu) | analog << 8;
	ADC(0x24) = (ADC(0x24) & 0xffffff80u) | digital;
}

int adc_open(uint32_t hz, uint32_t analog, uint32_t digital)
{
	/* Power the audio analog domain first (stock FUN_15800000, the same block the
	 * DAC uses). The mic ADC lives in that analog block and a cold boot / ADFU
	 * leaves it unpowered — without this the ADC never converts, so the capture
	 * DMA never completes (rectest/recwav appear to hang). */
	REG(PMU_BASE + 0x18) = (REG(PMU_BASE + 0x18) & ~0x20u) | 0x40;
	for (volatile int i = 0; i < 0x14; i++)
		;
	REG(0xc0000004) = (REG(0xc0000004) & ~4u) | 4; /* audio block out of reset */
	REG(0xc0000118) = (REG(0xc0000118) & ~0x1fu) | 0x1c; /* AUDIOPLL */
	REG(0xc000011c) = (REG(0xc000011c) & ~0x1fu) | 0x14;
	delay(20);
	REG(0xc0000004) |= 1;          /* ADC out of reset */
	REG(CMU_BASE + 0x0c) |= 1;     /* ADC clock */
	if (adc_rate(hz))
		return -1;
	/* input select 4 = mic (FUN_1586007e) */
	ADC(0x44) |= 0x4000000;
	ADC(0x44) |= 0x2a;
	/* left channel from input 4 with digital gain (FUN_15860124(4,1,1,0,0,0,gain,0)) */
	REG(CMU_BASE + 0x80) |= 0x1000;
	/* Replace the gain field, not OR it with a previous recording's gain. */
	ADC(0x24) = (ADC(0x24) & ~0x7fu) | 1u << 20 | 1u << 19 |
		(digital > 59 ? 59 : digital);
	ADC(0x34) = 4;
	ADC(0x00) |= 1;
	ADC(0x48) |= 0x8411a293;
	ADC(0x4c) |= 0x5f5;
	/* FUN at 0x0d8205e8 */
	ADC(0x04) = 0;
	REG(CMU_BASE + 0x80) &= ~0xc00u;
	/* FIFO: mono, DMA (FUN_0d820612) */
	ADC(0x08) = 0x13;
	/* mic path + analog gain (FUN_0d82058c(1, 0)), AGC flag off -> |0x10 as stock */
	ADC(0x3c) = (ADC(0x3c) & 0xfffffec0u) | 0x100 | 0x10 | 0xc;
	ADC(0x44) = (ADC(0x44) & 0xfff8803fu) | (analog & 0xf) << 8 | 0x70080;
	/* DMA channel: source = ADC FIFO (FUN_0d82062c, mono).
	 * VERIFIED config: reload mode, peripheral -> RAM (cfg 0x40000|0x8b). Do NOT
	 * set bit 20: the stock recorder's param_2<<20 argument comes from its mode
	 * field, which is 0 for our mono mic path — with bit 20 set the channel never
	 * completes (rectest reports TIMEOUT). */
	DMAC(4) &= ~1u;
	DMAC(0x00) = 0x40000 | 0x8b;
	DMAC(0x08) = 0xc0181000 + 0x18;
	DMAC(0x0c) = 0;
	DMAC(0x10) = 0;
	DMAC(0x18) = 0;
	running = pending = 0;
	return 0;
}

static int wait_complete(void)
{
	/* The ADC bring-up path must never hard-freeze the UI. A 4 KB recorder DMA
	 * buffer should finish in tens of ms at 24 kHz; this is intentionally very
	 * generous and reports failure instead of spinning forever if the FIFO/DMA
	 * setup is wrong for a rate or gain setting. */
	for (uint32_t i = 0; !(DMA_PEND & (1u << RDMA)); i++) {
		if (i > 12000000u)
			return -1;
		if ((i & 0xfff) == 0)
			wdt_feed();
	}
	DMA_PEND = 1u << RDMA;
	return 0;
}

/* Same queue-ahead protocol as audio_queue(): returns once the buffer queued two calls earlier
 * has been filled completely. */
int adc_queue(void *buf, uint32_t bytes)
{
	if (running && pending && wait_complete())
		return -1;
	DMAC(0x10) = (uint32_t)buf;
	DMAC(0x14) = 0;
	DMAC(0x18) = bytes;
	if (!running) {
		DMA_PEND = 1u << RDMA;
		DMAC(4) |= 1;
		running = 1;
		pending = 0;
		return 0;
	}
	pending = 1;
	return 0;
}

int adc_wait_one(void)
{
	return wait_complete();
}

void adc_close(void)
{
	DMAC(4) &= ~1u;
	DMA_PEND = 1u << RDMA;
	running = pending = 0;
	ADC(0x00) &= ~3u;
}
