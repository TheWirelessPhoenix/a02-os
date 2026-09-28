/* Streaming MP3 (minimp3) / WAV player. Decodes into rotating PCM buffers fed to audio_queue(). */
#include <string.h>
#include "player.h"
#include "audio.h"
#include "keys.h"
#include "guard.h"
#include "powermenu.h"
#include "ff.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#define INBUF 8192

static FIL fil;
char player_title[48], player_artist[40];
uint32_t player_secs, player_total_secs;
static mp3dec_t dec;
static uint8_t inbuf[INBUF] __attribute__((aligned(4)));
static int16_t out[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t pcm[3][MINIMP3_MAX_SAMPLES_PER_FRAME] __attribute__((aligned(4)));

static int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	if (n < e)
		return 0;
	for (size_t i = 0; i < e; i++) {
		char c = name[n - e + i];
		if (c >= 'a' && c <= 'z')
			c -= 32;
		if (c != ext[i])
			return 0;
	}
	return 1;
}

int player_supported(const char *name)
{
	return has_ext(name, ".MP3") || has_ext(name, ".WAV");
}

/* copy an ID3 text frame (ISO-8859-1 / UTF-8 / UTF-16) as printable ASCII */
static void id3_text(char *dst, int cap, const uint8_t *p, uint32_t len)
{
	int n = 0;
	if (!len)
		return;
	uint8_t enc = *p++;
	len--;
	int wide = enc == 1 || enc == 2;
	if (enc == 1 && len >= 2 && (p[0] == 0xff || p[0] == 0xfe)) {
		int be = p[0] == 0xfe;
		p += 2;
		len -= 2;
		for (uint32_t i = 0; i + 1 < len && n < cap - 1; i += 2) {
			uint32_t c = be ? (uint32_t)p[i] << 8 | p[i + 1] : (uint32_t)p[i + 1] << 8 | p[i];
			if (!c)
				break;
			dst[n++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
		}
	} else {
		for (uint32_t i = 0; i < len && n < cap - 1; i += wide ? 2 : 1) {
			uint8_t c = p[i];
			if (!c)
				break;
			if (c >= 0x80) {
				/* skip UTF-8 continuation bytes, show one '?' per non-ASCII char */
				if ((c & 0xc0) == 0x80)
					continue;
				c = '?';
			}
			dst[n++] = c >= 0x20 ? (char)c : ' ';
		}
	}
	dst[n] = 0;
}

/* read TIT2/TPE1 from an ID3v2.3/2.4 tag, then skip it (album art can be hundreds of KB) */
static void skip_id3(void)
{
	uint8_t h[10];
	UINT br;
	if (f_read(&fil, h, 10, &br) == FR_OK && br == 10 && !memcmp(h, "ID3", 3)) {
		uint32_t sz = (uint32_t)(h[6] & 0x7f) << 21 | (h[7] & 0x7f) << 14 | (h[8] & 0x7f) << 7 | (h[9] & 0x7f);
		uint32_t n = sz < INBUF ? sz : INBUF;
		int v4 = h[3] == 4;
		if (h[3] >= 3 && f_read(&fil, inbuf, n, &br) == FR_OK) {
			for (uint32_t o = 0; o + 10 <= br;) {
				const uint8_t *f = inbuf + o;
				uint32_t fs = v4 ? (uint32_t)(f[4] & 0x7f) << 21 | (f[5] & 0x7f) << 14 | (f[6] & 0x7f) << 7 | (f[7] & 0x7f)
				                 : (uint32_t)f[4] << 24 | f[5] << 16 | f[6] << 8 | f[7];
				if (!f[0] || fs == 0 || o + 10 + fs > br)
					break;
				if (!memcmp(f, "TIT2", 4))
					id3_text(player_title, sizeof(player_title), f + 10, fs);
				else if (!memcmp(f, "TPE1", 4))
					id3_text(player_artist, sizeof(player_artist), f + 10, fs);
				o += 10 + fs;
			}
		}
		f_lseek(&fil, 10 + sz + ((h[5] & 0x10) ? 10 : 0));
	} else {
		f_lseek(&fil, 0);
	}
}

static void levels(struct player_ui *ui, const int16_t *s, int frames)
{
	uint8_t lv[NBANDS];
	int per = frames / NBANDS;
	if (!ui->on_levels || per <= 0)
		return;
	for (int b = 0; b < NBANDS; b++) {
		uint32_t peak = 0;
		for (int i = b * per; i < (b + 1) * per; i += 4) {
			int32_t v = s[2 * i];
			if (v < 0)
				v = -v;
			if ((uint32_t)v > peak)
				peak = (uint32_t)v;
		}
		lv[b] = (uint8_t)(peak >> 7);
	}
	ui->on_levels(lv, NBANDS);
}

static uint32_t cur_hz; /* rate of the playing track, for restarting output after a pause */

static int other_key(struct player_ui *ui, int k)
{
	switch (k) {
	case KEY_NEXT:
		return PLAYER_NEXT;
	case KEY_PREV:
		return PLAYER_PREV;
	case KEY_BACK:
		return PLAYER_BACK;
	case KEY_MENU:
	case KEY_DOWN:
		if (ui->on_key)
			ui->on_key(k);
		break;
	}
	return PLAYER_CONTINUE;
}

/* Paused: output halted at once (reload-mode DMA would otherwise keep replaying). Only an
 * explicit Play resumes, and only onto an audible route (phones, or a speaker armed by that
 * Play with the jack settled empty). Jack changes while paused never resume anything. */
static int pause_loop(struct player_ui *ui)
{
	audio_halt();
	guard_quiet();
	if (ui->on_pause)
		ui->on_pause(1);
	for (;;) {
		guard_poll();
		guard_take_event();
		int k = keys_poll();
		if (k == KEY_PLAY) {
			guard_user_play();
			if (!guard_must_pause())
				break;
		} else if (k == KEY_POWER) {
			powermenu();
			if (ui->on_start)
				ui->on_start(cur_hz, 0, 0);
			if (ui->on_pause)
				ui->on_pause(1);
		} else if (k != KEY_NONE) {
			int r = other_key(ui, k);
			if (r != PLAYER_CONTINUE)
				return r;
		}
		delay(1);
	}
	/* Full DAC + DMA re-setup, as for a new track: a channel halted mid-buffer is not
	 * guaranteed to restart from just a new source address (it froze the player). */
	audio_start(cur_hz);
	if (ui->on_pause)
		ui->on_pause(0);
	return PLAYER_CONTINUE;
}

/* Hold Back: silence first, then the POWER menu; cancel repaints and stays paused. */
static int power_key(struct player_ui *ui)
{
	audio_halt();
	guard_quiet();
	powermenu();
	if (ui->on_start)
		ui->on_start(cur_hz, 0, 0); /* both players' start callbacks just repaint */
	return pause_loop(ui);
}

/* After each queued buffer: jack guard first, then keys. */
static int after_queue(struct player_ui *ui, int queued)
{
	guard_poll();
	if (queued < 0 || guard_must_pause())
		return pause_loop(ui);
	int k = keys_poll();
	if (k == KEY_PLAY)
		return pause_loop(ui);
	if (k == KEY_POWER)
		return power_key(ui);
	return other_key(ui, k);
}

static int guard_hook(void)
{
	guard_poll();
	return guard_must_pause();
}

static int play_mp3(struct player_ui *ui)
{
	uint32_t fill = 0, total = (uint32_t)f_size(&fil), pos;
	int b = 0, started = 0, eof = 0, r = PLAYER_DONE;
	mp3dec_frame_info_t info;
	UINT br;

	uint32_t frames_played = 0;
	mp3dec_init(&dec);
	skip_id3();
	pos = (uint32_t)f_tell(&fil);
	for (;;) {
		if (!eof && fill < INBUF / 2) {
			if (f_read(&fil, inbuf + fill, INBUF - fill, &br) != FR_OK)
				break;
			if (br == 0)
				eof = 1;
			fill += br;
			pos += br;
		}
		if (fill == 0)
			break;
		int samples = mp3dec_decode_frame(&dec, inbuf, (int)fill, out, &info);
		if (info.frame_bytes == 0) {
			if (eof)
				break;
			fill = 0; /* no sync in buffer: drop and refill */
			continue;
		}
		memmove(inbuf, inbuf + info.frame_bytes, fill - (uint32_t)info.frame_bytes);
		fill -= (uint32_t)info.frame_bytes;
		if (samples <= 0)
			continue;
		if (!started) {
			if (info.bitrate_kbps)
				player_total_secs = (total - (uint32_t)f_tell(&fil)) / ((uint32_t)info.bitrate_kbps * 125);
			cur_hz = (uint32_t)info.hz;
			audio_start(cur_hz);
			if (ui->on_start)
				ui->on_start((uint32_t)info.hz, (uint32_t)info.channels, (uint32_t)info.bitrate_kbps);
			started = 1;
		}
		int16_t *p = pcm[b];
		if (info.channels == 1) {
			for (int i = 0; i < samples; i++)
				p[2 * i] = p[2 * i + 1] = out[i];
		} else {
			memcpy(p, out, (size_t)samples * 4);
		}
		int q = audio_queue(p, (uint32_t)samples * 4);
		b = (b + 1) % 3;
		frames_played += (uint32_t)samples;
		player_secs = frames_played / (uint32_t)info.hz;
		levels(ui, p, samples);
		if (ui->on_progress)
			ui->on_progress(pos - fill, total);
		r = after_queue(ui, q);
		if (r != PLAYER_CONTINUE)
			break;
		r = PLAYER_DONE;
	}
	audio_stop();
	return r;
}

static int play_wav(struct player_ui *ui)
{
	uint8_t hdr[44];
	UINT br;
	int b = 0, r = PLAYER_DONE;

	if (f_read(&fil, hdr, 44, &br) != FR_OK || br != 44 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
		return PLAYER_ERROR;
	uint32_t rate = hdr[24] | hdr[25] << 8 | hdr[26] << 16 | (uint32_t)hdr[27] << 24;
	uint32_t ch = hdr[22], bits = hdr[34];
	if ((ch != 1 && ch != 2) || bits != 16 || !rate)
		return PLAYER_ERROR;
	uint32_t frame = ch * 2; /* bytes per file frame: mono recorder files are 2, stereo 4 */
	uint32_t total = (uint32_t)f_size(&fil), pos = 44;
	player_total_secs = (total - 44) / (rate * frame);
	cur_hz = rate;
	audio_start(rate);
	if (ui->on_start)
		ui->on_start(rate, ch, rate * ch * 16 / 1000);
	for (;;) {
		/* the DAC always takes stereo: read mono into the first half, then widen in place
		 * (back to front, so no sample is overwritten before it is copied) */
		uint32_t want = ch == 1 ? sizeof(pcm[b]) / 2 : sizeof(pcm[b]);
		if (f_read(&fil, pcm[b], want, &br) != FR_OK || br < frame)
			break;
		int n = (int)(br / frame);
		if (ch == 1)
			for (int i = n - 1; i >= 0; i--)
				pcm[b][2 * i] = pcm[b][2 * i + 1] = pcm[b][i];
		int q = audio_queue(pcm[b], (uint32_t)n * 4);
		levels(ui, pcm[b], n);
		pos += br;
		player_secs = (pos - 44) / (rate * frame);
		b = (b + 1) % 3;
		if (ui->on_progress)
			ui->on_progress(pos, total);
		r = after_queue(ui, q);
		if (r != PLAYER_CONTINUE)
			break;
		r = PLAYER_DONE;
	}
	audio_stop();
	return r;
}

int player_play(const char *path, struct player_ui *ui)
{
	int r;
	player_title[0] = player_artist[0] = 0;
	player_secs = player_total_secs = 0;
	if (f_open(&fil, path, FA_READ) != FR_OK)
		return PLAYER_ERROR;
	guard_settle();
	guard_take_event(); /* only changes during this track count */
	audio_set_wait_hook(guard_hook);
	r = has_ext(path, ".WAV") ? play_wav(ui) : play_mp3(ui);
	audio_set_wait_hook(0);
	guard_quiet();
	f_close(&fil);
	return r;
}
