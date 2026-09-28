/* Lecture recorder engine (step 2).
 *
 * adc.c captures mono mic data one sample per 32-bit word; the sample is the high 16
 * bits as SIGNED two's complement (0x0000 = silence). See convert() below.
 * Here we convert in place to signed 16-bit PCM and stream it to a WAV file.
 *
 * adc_queue() is reload-mode queue-ahead: when it returns, the buffer passed *two* calls
 * earlier has been filled completely, and the buffer just passed is armed as the next
 * DMA target. So we rotate three buffers and write the one that just completed.
 *
 * Error handling: every fallible step is checked, the error is reported through
 * rec_error() (and the negative return), and finalize() tracks whether the header
 * rewrite + sync + close really completed so the UI can tell SAVED from SAVE FAILED.
 * See recorder.h for the error-code table.
 */
#include "recorder.h"
#include "adc.h"
#include "ff.h"
#include <string.h>

#define NBUF  3
#define BUFSZ 4096 /* raw bytes; yields BUFSZ/2 PCM bytes = 32 ms at 32 kHz */

static uint8_t buf[NBUF][BUFSZ] __attribute__((aligned(4)));
static FIL fil;
static int active, err, paused, finalized, idx;
static uint32_t pcm_bytes, hz_used;
static uint8_t level;

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void wav_header(uint8_t *h)
{
	uint32_t rate = hz_used;
	uint32_t data = pcm_bytes > REC_MAX_BYTES ? REC_MAX_BYTES : pcm_bytes;

	memset(h, 0, 44);
	memcpy(h + 0, "RIFF", 4);
	put32(h + 4, 36 + data);
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "fmt ", 4);
	put32(h + 16, 16);
	put16(h + 20, 1);        /* PCM */
	put16(h + 22, 1);        /* mono */
	put32(h + 24, rate);
	put32(h + 28, rate * 2); /* byte rate, 16-bit mono */
	put16(h + 32, 2);        /* block align */
	put16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	put32(h + 40, data);
}

/* One 32-bit word -> one 16-bit PCM sample. Writing halfword i only ever touches word i/2,
 * which has already been read, so this is safe to do in place.
 *
 * Format (verified on device 2026-09-27 by dumping the raw ADC buffer): the sample is the
 * high 16 bits interpreted as SIGNED two's complement — 0x0000 is silence, 0xFFFF is -1,
 * 0x8000 is -32768. (An earlier version subtracted 32768, which added a -32768 DC offset
 * and clipped everything; the values are not unsigned.) */
static void convert(uint8_t *b, uint32_t bytes)
{
	uint32_t n = bytes / 4;
	volatile uint32_t *in = (volatile uint32_t *)b;
	uint16_t *out = (uint16_t *)b;
	uint32_t peak = 0;

	for (uint32_t i = 0; i < n; i++) {
		int16_t v = (int16_t)(in[i] >> 16);
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (a > peak)
			peak = a;
		out[i] = (uint16_t)v;
	}
	/* -32768 has no positive counterpart in int16; saturate at full scale so the
	 * level is 255 rather than wrapping through 32768 >> 7 to 0. */
	if (peak > 32767u)
		peak = 32767u;
	level = (uint8_t)(peak >> 7); /* 32767 -> 255 */
}

/* Rewrite the 44-byte header with the sizes we really got, sync, and close.
 * Used both for a clean stop and for the error paths, so a lecture cut short by a
 * full card (or the 4 GB WAV cap) still ends up a playable file instead of a zero-length
 * one. Returns 0 only when every step succeeded; rec_finalized() reflects that. */
static int finalize(void)
{
	uint8_t h[44];
	UINT bw;
	int ok = 1;

	wav_header(h);
	if (f_lseek(&fil, 0) != FR_OK || f_write(&fil, h, 44, &bw) != FR_OK || bw != 44 ||
	    f_sync(&fil) != FR_OK)
		ok = 0;
	if (f_close(&fil) != FR_OK)
		ok = 0;
	if (!ok)
		err = -7; /* header/sync/close failure: the file may be truncated or stale */
	finalized = ok;
	active = 0;
	paused = 0;
	return ok ? 0 : -7;
}

int rec_start(const char *path, uint32_t hz)
{
	uint8_t h[44];
	UINT bw;

	active = 0;
	err = 0;
	paused = 0;
	finalized = 0;
	idx = 0;
	pcm_bytes = 0;
	level = 0;
	hz_used = hz;

	if (hz == 0)
		return err = -1; /* a 0 Hz header is not a WAV; refuse up front */
	if (adc_open(hz, REC_AGAIN, REC_DGAIN))
		return err = -4;
	/* Exclusive create: a lecture file is never overwritten, not even if next_num()
	 * raced or the caller passed a stale path. */
	{
		FRESULT fr = f_open(&fil, path, FA_CREATE_NEW | FA_WRITE);
		if (fr != FR_OK) {
			adc_close();
			return err = (fr == FR_EXIST ? -10 : -2);
		}
	}
	wav_header(h); /* placeholder: sizes are rewritten by finalize() */
	if (f_write(&fil, h, 44, &bw) != FR_OK || bw != 44) {
		f_close(&fil);
		adc_close();
		return err = -3;
	}
	if (adc_queue(buf[0], BUFSZ) || adc_queue(buf[1], BUFSZ)) {
		f_close(&fil);
		adc_close();
		return err = -4;
	}
	active = 1;
	return 0;
}

int rec_pump(void)
{
	uint8_t *b;
	UINT bw;
	FRESULT fr;

	if (!active)
		return -1;
	if (paused)
		return 0; /* capture suspended: the file stays open, the timer stays frozen */
	if (pcm_bytes >= REC_MAX_BYTES) { /* stop before the 32-bit RIFF field would wrap */
		err = -8;
		adc_close();
		finalize();
		return -8;
	}
	if (adc_queue(buf[(idx + 2) % NBUF], BUFSZ)) { /* on success, buf[idx] is full */
		err = -5; /* ADC DMA timeout */
		adc_close();
		finalize();
		return -5;
	}
	b = buf[idx];
	idx = (idx + 1) % NBUF;
	convert(b, BUFSZ);
	fr = f_write(&fil, b, BUFSZ / 2, &bw);
	if (fr != FR_OK || bw != BUFSZ / 2) {
		if (fr == FR_OK)
			pcm_bytes += bw; /* card filled mid-buffer: count what did land */
		err = -6;            /* card full or write error */
		adc_close();
		finalize();
		return -6;
	}
	pcm_bytes += BUFSZ / 2;
	return 0;
}

int rec_pause(void)
{
	if (!active || paused)
		return -1;
	adc_close(); /* mic off, no power burn, nothing written */
	if (f_sync(&fil) != FR_OK) {
		/* The card just stopped accepting data. Save what we have and stop honestly
		 * rather than sit in PAUSED on a file we cannot finish. */
		finalize();
		return -7;
	}
	paused = 1;
	return 0;
}

int rec_resume(void)
{
	if (!active || !paused)
		return -1;
	if (adc_open(hz_used, REC_AGAIN, REC_DGAIN))
		return -9; /* keep the file open and paused; the UI can retry */
	if (adc_queue(buf[idx], BUFSZ) || adc_queue(buf[(idx + 1) % NBUF], BUFSZ)) {
		adc_close();
		return -9;
	}
	paused = 0;
	return 0;
}

void rec_stop(void)
{
	if (!active)
		return;
	adc_close();
	finalize();
}

int rec_active(void) { return active; }
int rec_paused(void) { return paused; }
int rec_error(void) { return err; }
int rec_finalized(void) { return finalized; }
uint32_t rec_pcm_bytes(void) { return pcm_bytes; }
uint8_t rec_level(void) { return level; }
uint32_t rec_secs(void) { return hz_used ? pcm_bytes / 2 / hz_used : 0; }
