/* Lecture recorder app.
 *
 * The five screens here are the ones agreed in the recorder UI design: Ready, Recording,
 * Paused, Saved and SD-full. Layout constants are the mockup's device pixels, so this and
 * the mockup are 1:1 (the mockup canvas is 128x160 with the same 14 px header and 12 px
 * hint bar). Colors come from the active theme in ui.c.
 *
 * Redrawing strategy: the LCD is a paced FIFO, so static parts are drawn once per screen and
 * only the live parts (timer, meter, waveform) are touched per pump. The meter grows/shrinks
 * by a few rectangles, and the waveform scrolls one column per buffer.
 */
#include <string.h>
#include "recapp.h"
#include "ui.h"
#include "keys.h"
#include "adc.h"
#include "recorder.h"
#include "ff.h"
#include "player.h"
#include "guard.h"

#define LCD_W_ 128

/* ---- layout (mockup coordinates) ---- */
#define METER_X 8
#define METER_Y 62
#define METER_W (LCD_W_ - 16)
#define METER_H 7
#define MIC_X  30
#define MIC_Y  91
#define MIC_W  (LCD_W_ - 40)
#define MIC_H  5
#define SC_X 8
#define SC_Y 76
#define SC_W (LCD_W_ - 16)
#define SC_H 34

enum { RS_READY, RS_REC, RS_PAUSED, RS_SAVED, RS_FULL };

static int rs = RS_READY;

/* ---- file bookkeeping ---- */
static char fdir[] = "/REC";
static char fname[16];  /* "LECT0001.WAV" */
static char fpath[24];  /* "/REC/LECT0001.WAV" */
static char mpath[24];  /* "/REC/LECT0001.MRK" */
static int fnum = 1;
static int marks;

/* ---- live display state ---- */
static uint8_t hist[SC_W];
static int hpos, m_w, m_peak, m_x, m_y, m_wd, m_h;
static uint8_t peak_hold, level_cur;
static int hold_cnt, pumps, last_sec, mark_flash, blink_on;
static uint32_t saved_bytes, saved_secs;

/* ---------- tiny string helpers (no printf on this box) ---------- */

static char *cp(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	*p = 0;
	return p;
}

/* h:mm:ss, always 7 chars so the big timer never changes width */
static char *hms(char *b, uint32_t s)
{
	char t[12], *p = b;
	num_str(t, s / 3600);
	p = cp(p, t);
	*p++ = ':';
	num_str(t, (s / 60) % 60);
	if (t[1] == 0)
		*p++ = '0';
	p = cp(p, t);
	*p++ = ':';
	num_str(t, s % 60);
	if (t[1] == 0)
		*p++ = '0';
	cp(p, t);
	return b;
}

/* m:ss */
static void ms_str(char *b, uint32_t s)
{
	char t[12], *p = b;
	num_str(t, s / 60);
	p = cp(p, t);
	*p++ = ':';
	num_str(t, s % 60);
	if (t[1] == 0)
		*p++ = '0';
	cp(p, t);
}

static void num4(char *b, int v) /* zero-padded 4 digits */
{
	b[0] = (char)('0' + (v / 1000) % 10);
	b[1] = (char)('0' + (v / 100) % 10);
	b[2] = (char)('0' + (v / 10) % 10);
	b[3] = (char)('0' + v % 10);
	b[4] = 0;
}

/* "18.9 MB" (or GB past a gigabyte) */
static void size_str(char *b, uint32_t bytes)
{
	char t[12], *p = b;
	int gb = bytes >= (1u << 30);
	uint32_t whole = gb ? (bytes >> 30) : (bytes >> 20);
	uint32_t d = gb ? (uint32_t)(((uint64_t)(bytes & 0x3fffffffu) * 10u) >> 30) : (uint32_t)(((bytes & 0xfffffu) * 10u) >> 20);
	num_str(t, whole);
	p = cp(p, t);
	*p++ = '.';
	*p++ = (char)('0' + d);
	cp(p, gb ? " GB" : " MB");
}

/* "SD 12.4 GB FREE" */
static void space_str(char *b, uint64_t fb)
{
	char t[12], *p = cp(b, "SD ");
	int gb = fb >= (1u << 30);
	uint32_t whole = gb ? (fb >> 30) : (fb >> 20);
	uint32_t d = gb ? (uint32_t)(((fb & 0x3fffffffu) * 10u) >> 30) : (uint32_t)(((fb & 0xfffffu) * 10u) >> 20);
	num_str(t, whole);
	p = cp(p, t);
	*p++ = '.';
	*p++ = (char)('0' + d);
	cp(p, gb ? " GB FREE" : " MB FREE");
}

static void center(const char *s, int y, int scale, uint16_t fg, uint16_t bg)
{
	lcd_text((LCD_W_ - 6 * (int)strlen(s) * scale) / 2, y, s, scale, fg, bg);
}

static void right(const char *s, int y, uint16_t fg, uint16_t bg)
{
	lcd_text(LCD_W_ - 8 - 6 * (int)strlen(s), y, s, 1, fg, bg);
}

/* ---------- level meter (continuous bar, 3 color zones, peak hold) ---------- */

static void meter_geom(int x, int y, int w, int h)
{
	m_x = x;
	m_y = y;
	m_wd = w;
	m_h = h;
	m_w = 0;
	m_peak = -1;
	lcd_rect(x, y, w, h, T->surface);
}

static void meter_grow(int a, int b)
{
	int b1 = m_wd * 66 / 100, b2 = m_wd * 86 / 100, z0[3], z1[3];
	z0[0] = 0;
	z0[1] = b1;
	z0[2] = b2;
	z1[0] = b1;
	z1[1] = b2;
	z1[2] = m_wd;
	for (int z = 0; z < 3; z++) {
		int s = a > z0[z] ? a : z0[z];
		int e = b < z1[z] ? b : z1[z];
		if (e > s)
			lcd_rect(m_x + s, m_y, e - s, m_h, z == 0 ? T->text : z == 1 ? T->accent : T->rec);
	}
}

static void meter_full(int w)
{
	int h = m_h;
	meter_geom(m_x, m_y, m_wd, h);
	if (w > 0)
		meter_grow(0, w);
	m_w = w;
}

static void meter_set(int lv) /* lv 0..255 */
{
	int w = (m_wd * lv) / 255;
	int px = (m_wd * peak_hold) / 255;
	if (w > m_w) {
		meter_grow(m_w, w);
	} else if (w < m_w) {
		lcd_rect(m_x + w, m_y, m_w - w, m_h, T->surface);
	}
	m_w = w;
	if (px != m_peak) {
		if (m_peak >= 0)
			lcd_rect(m_x + m_peak, m_y - 2, 2, 1, T->bg);
		if (px >= 0)
			lcd_rect(m_x + px, m_y - 2, 2, 1, T->rec);
		m_peak = px;
	}
}

/* peak hold: sit at the peak for ~1 s, then fall slowly */
static void levels(int lv)
{
	level_cur = (uint8_t)lv;
	if (level_cur > peak_hold) {
		peak_hold = level_cur;
		hold_cnt = 24;
	} else if (hold_cnt) {
		hold_cnt--;
	} else {
		peak_hold = peak_hold > 3 ? (uint8_t)(peak_hold - 3) : 0;
		if (peak_hold < level_cur)
			peak_hold = level_cur;
	}
}

/* ---------- scrolling waveform ---------- */

static void scope_full(void)
{
	lcd_rect(SC_X, SC_Y, SC_W, SC_H, T->surface);
	for (int i = 0; i < SC_W; i += 16)
		lcd_rect(SC_X + i, SC_Y + (SC_H >> 1), 1, 1, T->dim);
	for (int i = 0; i < SC_W; i++) {
		int v = hist[i]; /* preserve sweep columns across pause/resume */
		int amp = v * (SC_H / 2 - 1) / 255;
		if (amp < 1)
			amp = 1;
		lcd_rect(SC_X + i, SC_Y + (SC_H >> 1) - amp, 1, amp * 2, T->accent);
	}
}

static void scope_push(int lv)
{
	int amp;
	hist[hpos] = (uint8_t)lv;
	lcd_rect(SC_X + hpos, SC_Y, 1, SC_H, T->surface);
	amp = lv * (SC_H / 2 - 1) / 255;
	if (amp < 1)
		amp = 1;
	lcd_rect(SC_X + hpos, SC_Y + (SC_H >> 1) - amp, 1, amp * 2, T->accent);
	hpos = (hpos + 1) % SC_W;
}

/* ---------- Ready screen: live mic check before you commit to a lecture ---------- */

static uint8_t pbuf[3][2048] __attribute__((aligned(4)));
static int peek, pidx;

static void peek_start(void)
{
	peek = 0;
	pidx = 0;
	if (adc_open(REC_RATE, REC_AGAIN, REC_DGAIN))
		return;
	if (adc_queue(pbuf[0], sizeof(pbuf[0])) || adc_queue(pbuf[1], sizeof(pbuf[0]))) {
		adc_close();
		return;
	}
	peek = 1;
}

static void peek_stop(void)
{
	if (peek) {
		adc_close();
		peek = 0;
	}
}

/* peak of the completed buffer, same signed-16 reading as recorder.c */
static int peek_pump(void)
{
	volatile uint32_t *in;
	uint32_t peak = 0;
	if (!peek)
		return 0;
	if (adc_queue(pbuf[(pidx + 2) % 3], sizeof(pbuf[0]))) {
		peek_stop(); /* don't repeatedly block on a failed ADC */
		return 0;
	}
	in = (volatile uint32_t *)pbuf[pidx];
	pidx = (pidx + 1) % 3;
	for (int i = 0; i < 512; i++) {
		int16_t v = (int16_t)(in[i] >> 16);
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (a > peak)
			peak = a;
	}
	return peak >= 32768u ? 255 : (int)(peak >> 7);
}

/* Find the next free LECT#### number in /REC. Returns 0 when all 9999 numbers are
 * used; rec_start_now() turns that into the NAME LIMIT screen instead of wrapping
 * to 1 and overwriting an existing lecture. */
static int next_num(void)
{
	DIR d;
	FILINFO fno;
	int max = 0;
	if (f_opendir(&d, fdir) != FR_OK)
		return 1;
	while (f_readdir(&d, &fno) == FR_OK && fno.fname[0]) {
		const char *n = fno.fname;
		if (memcmp(n, "LECT", 4))
			continue;
		int v = 0, ok = 1;
		for (int i = 4; i < 8; i++) {
			if (n[i] < '0' || n[i] > '9') {
				ok = 0;
				break;
			}
			v = v * 10 + (n[i] - '0');
		}
		if (ok && v > max)
			max = v;
	}
	f_closedir(&d);
	return max >= 9999 ? 0 : max + 1;
}

static void build_names(void)
{
	fnum = next_num();
	strcpy(fname, "LECT");
	num4(fname + 4, fnum);
	strcpy(fname + 8, ".WAV");
	cp(cp(cp(fpath, fdir), "/"), fname);
	marks = 0;
}

static uint64_t free_bytes(void)
{
	DWORD nclst = 0;
	FATFS *fs = 0;
	if (f_getfree("", &nclst, &fs) != FR_OK || !fs)
		return 0;
	return (uint64_t)nclst * fs->csize * 512u; /* ss is fixed at 512 (FF_MAX_SS == FF_MIN_SS) */
}

static void draw_ready(void)
{
	char b[24], *p;
	int cx = LCD_W_ / 2;
	ui_rec_header("RECORDER", 0);
	ui_body_clear();
	/* mic pictogram */
	lcd_rect(cx - 4, 22, 9, 18, T->text);
	lcd_rect(cx - 7, 30, 3, 10, T->text);
	lcd_rect(cx + 6, 30, 3, 10, T->text);
	lcd_rect(cx - 7, 38, 15, 3, T->text);
	lcd_rect(cx - 1, 41, 3, 7, T->text);
	lcd_rect(cx - 6, 48, 13, 3, T->text);
	p = cp(b, "LECTURE ");
	num4(p, fnum); /* "LECTURE 0001", without the WAV extension */
	center(b, 62, 1, T->text, T->bg);
	space_str(b, free_bytes());
	center(b, 74, 1, T->dim, T->bg);
	lcd_text(8, 90, "MIC", 1, T->dim, T->bg);
	meter_geom(MIC_X, MIC_Y, MIC_W, MIC_H);
	center("READY", 106, 2, T->accent, T->bg);
	ui_hints("BACK", "PLAY REC");
}

/* ---------- Recording / Paused ---------- */

static void rec_chrome(const char *title, int dot, uint16_t timer_fg)
{
	char b[16];
	ui_rec_header(title, dot);
	ui_body_clear();
	hms(b, rec_secs());
	center(b, 24, 2, timer_fg, T->bg);
	center(fname, 46, 1, T->dim, T->bg);
	meter_geom(METER_X, METER_Y, METER_W, METER_H);
	scope_full();
	lcd_rect(8, 116, METER_W, 1, T->line);
	lcd_text(8, 122, "LENGTH", 1, T->dim, T->bg);
	ms_str(b, rec_secs());
	right(b, 122, T->text, T->bg);
	lcd_text(8, 134, "SIZE", 1, T->dim, T->bg);
	size_str(b, rec_pcm_bytes());
	right(b, 134, T->text, T->bg);
}

static void draw_rec(void)
{
	rec_chrome("RECORDING", 1, T->text);
	last_sec = (int)rec_secs();
	meter_set(level_cur);
	ui_hints("BACK", "PLAY PAUSE");
}

static void draw_paused(void)
{
	char b[16];
	rec_chrome("PAUSED", 1, T->dim);
	meter_full((METER_W * level_cur) / 255); /* frozen level */
	lcd_text(8, 122, "PAUSED", 1, T->rec, T->bg);
	ms_str(b, rec_secs());
	right(b, 122, T->dim, T->bg);
	lcd_rect(8, 134, 112, 9, T->bg);
	ui_hints("BACK", "PLAY RESUME");
}

/* live parts: timer, size, meter, one waveform column */
static void rec_update(void)
{
	char b[16];
	uint32_t s = rec_secs();
	if ((int)s != last_sec) {
		last_sec = (int)s;
		lcd_rect(22, 24, 84, 16, T->bg);
		hms(b, s);
		center(b, 24, 2, T->text, T->bg);
		ms_str(b, s);
		lcd_rect(60, 122, 60, 9, T->bg);
		right(b, 122, T->text, T->bg);
		size_str(b, rec_pcm_bytes());
		lcd_rect(50, 134, 70, 9, T->bg);
		right(b, 134, T->text, T->bg);
	}
	levels(rec_level());
	scope_push(level_cur);
	meter_set(level_cur);
}

/* ---------- Saved ---------- */

static void draw_saved(void)
{
	char b[24];
	int cx = LCD_W_ / 2;
	ui_rec_header("SAVED", 0);
	ui_body_clear();
	/* tick badge */
	lcd_rect(cx - 13, 22, 26, 3, T->accent);
	lcd_rect(cx - 13, 43, 26, 3, T->accent);
	lcd_rect(cx - 13, 22, 3, 24, T->accent);
	lcd_rect(cx + 10, 22, 3, 24, T->accent);
	for (int i = 0; i < 5; i++) {
		static const int8_t dx[5] = { -6, -3, 0, 3, 6 };
		static const int8_t dy[5] = { 32, 35, 32, 29, 26 };
		lcd_rect(cx + dx[i], dy[i], 3, 3, T->accent);
	}
	center(fname, 56, 1, T->text, T->bg);
	center(fdir, 68, 1, T->dim, T->bg);
	hms(b, saved_secs);
	center(b, 84, 2, T->text, T->bg);
	size_str(b, saved_bytes);
	center(b, 108, 1, T->dim, T->bg);
	center("PLAY IT BACK", 124, 1, T->accent, T->bg);
	ui_hints("BACK", "PLAY PLAY");
}

/* ---------- SD full / write error / other engine failures ---------- */

static const char *err_title = "SD FULL";
static const char *err_sub = "CARD IS FULL";
static const char *err_hint1 = "RECORDING STOPPED";
static const char *err_hint2 = "FREE UP SPACE";

/* Pick honest wording for the engine's error code instead of calling everything SD FULL. */
static void set_engine_error(int e)
{
	err_hint1 = "RECORDING STOPPED";
	switch (e) {
	case -3:
	case -7:
		err_title = "SAVE ERROR";
		err_sub = "WRITE PROBLEM";
		err_hint2 = "CHECK THE CARD";
		break;
	case -5:
		err_title = "MIC ERROR";
		err_sub = "MICROPHONE LOST";
		err_hint2 = "TRY AGAIN";
		break;
	case -8:
		err_title = "FILE LIMIT";
		err_sub = "WAV 4 GB LIMIT";
		err_hint2 = "SAVED AS IS";
		break;
	case -2:
	case -10:
		err_title = "SD ERROR";
		err_sub = "CANNOT CREATE FILE";
		err_hint2 = "CHECK THE CARD";
		break;
	default: /* -6 card full / write error */
		err_title = "SD FULL";
		err_sub = "CARD IS FULL";
		err_hint2 = "FREE UP SPACE";
		break;
	}
}

static void draw_error(void)
{
	char b[24], *p;
	int cx = LCD_W_ / 2;
	ui_rec_header("RECORDER", 0);
	ui_body_clear();
	/* warning triangle with a cut-out "!" */
	lcd_rect(cx - 3, 22, 6, 3, T->rec);
	lcd_rect(cx - 6, 25, 12, 3, T->rec);
	lcd_rect(cx - 9, 28, 18, 3, T->rec);
	lcd_rect(cx - 12, 31, 24, 3, T->rec);
	lcd_rect(cx - 15, 34, 30, 3, T->rec);
	lcd_rect(cx - 1, 27, 2, 5, T->bg);
	lcd_rect(cx - 1, 34, 2, 2, T->bg);
	center(err_title, 50, 2, T->rec, T->bg);
	center(err_sub, 68, 1, T->dim, T->bg);
	/* Only claim SAVED when finalize() really wrote the header, synced and closed. */
	p = cp(b, fname);
	cp(p, rec_finalized() ? " SAVED" : " NOT SAVED");
	center(b, 84, 1, T->text, T->bg);
	lcd_rect(8, 110, METER_W, 1, T->line);
	lcd_text(8, 118, err_hint1, 1, T->text, T->bg);
	lcd_text(8, 130, err_hint2, 1, T->dim, T->bg);
	ui_hints("BACK", "");
}

/* Leave the recording states for the error screen, remembering how much landed. */
static void fail_to_error(int e)
{
	saved_bytes = rec_pcm_bytes();
	saved_secs = rec_secs();
	set_engine_error(e);
	rs = RS_FULL;
	draw_error();
}

/* ---------- playback of the file you just made ---------- */

static int sv_bar[NBANDS], sv_last, sv_sec = -1, sv_tick;

static void sv_times(void)
{
	char a[12], b[12];
	ms_str(a, player_secs);
	ms_str(b, player_total_secs);
	lcd_rect(0, 131, LCD_W_, 9, T->bg);
	lcd_text(6, 131, a, 1, T->dim, T->bg);
	lcd_text(LCD_W_ - 6 - 6 * (int)strlen(b), 131, b, 1, T->dim, T->bg);
}

static void sv_start(uint32_t hz, uint32_t ch, uint32_t kbps)
{
	(void)hz;
	(void)ch;
	(void)kbps;
	ui_header("PLAYBACK", 1);
	ui_body_clear();
	center(fname, 26, 1, T->text, T->bg);
	lcd_rect(6, 44, LCD_W_ - 12, 40, T->surface);
	for (int i = 0; i < NBANDS; i++)
		sv_bar[i] = 0;
	sv_last = 0;
	sv_sec = -1;
	sv_times();
	ui_hints("< BACK", "");
}

static void sv_progress(uint32_t pos, uint32_t total)
{
	int w = total ? (int)((uint64_t)pos * (LCD_W_ - 12) / total) : 0;
	if (w > sv_last) {
		lcd_rect(6 + sv_last, 125, w - sv_last, 3, T->accent);
		sv_last = w;
	}
	if ((int)player_secs != sv_sec) {
		sv_sec = (int)player_secs;
		sv_times();
	}
}

static void sv_pause(int paused)
{
	const char *s = paused ? "PAUSED" : "";
	lcd_rect(50, 131, 28, 9, T->bg);
	lcd_text(LCD_W_ / 2 - 3 * (int)strlen(s), 131, s, 1, T->accent, T->bg);
}

static void sv_key(int k)
{
	(void)k;
}

static void sv_levels(const uint8_t *lv, int n)
{
	int x0 = 12, base = 80;
	if (++sv_tick & 1)
		return; /* every other frame, like the now-playing visualiser */
	for (int i = 0; i < n && i < NBANDS; i++) {
		int h = lv[i] * 32 / 255;
		if (h < sv_bar[i] - 3)
			h = sv_bar[i] - 3;
		if (h == sv_bar[i])
			continue;
		int x = x0 + i * 9;
		if (h > sv_bar[i])
			lcd_rect(x, base - h, 6, h - sv_bar[i], T->accent);
		else
			lcd_rect(x, base - sv_bar[i], 6, sv_bar[i] - h, T->surface);
		sv_bar[i] = h;
	}
}

static struct player_ui sv_ui = { sv_start, sv_progress, sv_pause, sv_key, sv_levels, 0 };

/* ---------- marks: a plain text sidecar with the seconds of each M press ---------- */

/* Append "h:mm:ss MARK n\n" to the sidecar. Returns 1 only when the whole line was
 * written, synced and closed; the caller must not count or acknowledge a failed mark. */
static int mark_add(void)
{
	FIL f;
	UINT bw;
	char t[28], n[12];
	size_t len, nl;

	if (!rec_active())
		return 0; /* a mark outside a live take has no timestamp to record */
	if (!marks) {
		/* first mark: create the sidecar, then append to it */
		if (f_open(&f, mpath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
			return 0;
	} else if (f_open(&f, mpath, FA_OPEN_APPEND | FA_WRITE) != FR_OK) {
		return 0;
	}
	hms(t, rec_secs());
	len = strlen(t); /* 7 chars */
	memcpy(t + len, " MARK ", 6);
	len += 6;
	num_str(n, (uint32_t)(marks + 1));
	nl = strlen(n);
	if (len + nl + 2 > sizeof(t)) { /* should be unreachable; never overflow the line */
		f_close(&f);
		return 0;
	}
	memcpy(t + len, n, nl);
	len += nl;
	t[len++] = '\n';
	/* One close on every path: a failed f_close must not be followed by a second one. */
	if (f_write(&f, t, (UINT)len, &bw) != FR_OK || bw != len || f_sync(&f) != FR_OK ||
	    f_close(&f) != FR_OK)
		return 0;
	marks++;
	return 1;
}

/* ---------- transitions ---------- */

static void saved_from_rec(void)
{
	rec_stop();
	/* Claim SAVED only when the engine confirmed header + sync + close. */
	if (rec_error() || !rec_finalized()) {
		fail_to_error(rec_error() ? rec_error() : -7);
		return;
	}
	saved_bytes = rec_pcm_bytes();
	saved_secs = rec_secs();
	rs = RS_SAVED;
	draw_saved();
}

/* All 9999 LECT numbers are taken: refuse rather than wrap to 1 and overwrite. */
static void draw_name_limit(void)
{
	strcpy(fname, "NO FREE NAME"); /* there is no lecture number to show */
	err_title = "NAME LIMIT";
	err_sub = "9999 FILES EXIST";
	err_hint1 = "DELETE OLD FILES";
	err_hint2 = "THEN RECORD AGAIN";
	rs = RS_FULL;
	draw_error();
}

static void rec_start_now(void)
{
	peek_stop();
	peak_hold = level_cur = 0;
	hold_cnt = 0;
	hpos = 0;
	memset(hist, 0, sizeof(hist));
	pumps = 0;
	mark_flash = 0;
	blink_on = 1;
	if (!fnum) {
		draw_name_limit();
		return;
	}
	if (rec_start(fpath, REC_RATE)) {
		fail_to_error(rec_error());
		return;
	}
	rs = RS_REC;
	draw_rec();
}

void recapp_open(void)
{
	FRESULT fr = f_mkdir(fdir); /* FR_EXIST is fine and expected */
	(void)fr;
	build_names();
	strcpy(mpath, fpath); /* "LECT0001.MRK" beside the WAV */
	mpath[strlen(mpath) - 3] = 'M';
	mpath[strlen(mpath) - 2] = 'R';
	mpath[strlen(mpath) - 1] = 'K';
	rs = RS_READY;
	peak_hold = level_cur = 0;
	hold_cnt = 0;
	pumps = 0;
	draw_ready();
	peek_start();
}

static void leave(void)
{
	peek_stop();
	if (rec_active())
		rec_stop();
	rs = RS_READY;
}

/* ---------- per-iteration work ---------- */

int recapp_pump(void)
{
	pumps++;
	switch (rs) {
	case RS_READY:
		if (peek) {
			levels(peek_pump());
			meter_set(level_cur);
		}
		break;
	case RS_REC: {
		int r = rec_pump();
		if (r < 0) {
			fail_to_error(rec_error());
			return 1;
		}
		rec_update();
		if (!(pumps % 24) && blink_on) { /* ~1 Hz REC dot */
			blink_on = 0;
			ui_rec_header("RECORDING", 0);
		} else if (!(pumps % 24)) {
			blink_on = 1;
			ui_rec_header("RECORDING", 1);
		}
		if (mark_flash && --mark_flash == 0) {
			char b[16];
			size_str(b, rec_pcm_bytes());
			lcd_rect(40, 134, 80, 9, T->bg);
			right(b, 134, T->text, T->bg);
		}
		break;
	}
	case RS_PAUSED:
		if (!(pumps % 500)) {
			blink_on = !blink_on;
			ui_rec_header("PAUSED", blink_on);
		}
		break;
	default:
		break;
	}
	return 0;
}

/* ---------- keys ---------- */

int recapp_key(int k)
{
	switch (rs) {
	case RS_READY:
		if (k == KEY_PLAY || k == KEY_NEXT) {
			rec_start_now();
		} else if (k == KEY_BACK || k == KEY_PREV) {
			leave();
			return 0;
		}
		return 1;

	case RS_REC:
		if (k == KEY_PLAY || k == KEY_NEXT) {
			if (rec_pause() == 0) {
				rs = RS_PAUSED;
				pumps = 0;
				blink_on = 1;
				draw_paused();
			} else {
				fail_to_error(rec_error()); /* card stopped accepting data */
			}
		} else if (k == KEY_MENU) {
			char b[16];
			lcd_rect(40, 134, 80, 9, T->bg);
			if (mark_add()) { /* only acknowledge a mark that really landed */
				cp(b, "MARK ");
				num_str(b + 5, (uint32_t)marks);
				right(b, 134, T->accent, T->bg);
			} else {
				right("MARK FAILED", 134, T->rec, T->bg);
			}
			mark_flash = 24;
		} else if (k == KEY_BACK || k == KEY_PREV) {
			saved_from_rec();
		}
		return 1;

	case RS_PAUSED:
		if (k == KEY_PLAY || k == KEY_NEXT) {
			if (rec_resume() == 0) {
				rs = RS_REC;
				pumps = 0;
				draw_rec();
			}
		} else if (k == KEY_MENU || k == KEY_BACK || k == KEY_PREV) {
			saved_from_rec();
		}
		return 1;

	case RS_SAVED:
		if (k == KEY_PLAY || k == KEY_NEXT) {
			guard_user_play();
			player_play(fpath, &sv_ui);
			guard_off();
			draw_saved();
		} else if (k == KEY_BACK || k == KEY_PREV) {
			return 0;
		}
		return 1;

	case RS_FULL:
		if (k == KEY_BACK || k == KEY_PREV) {
			return 0;
		}
		return 1;
	}
	return 1;
}

void recapp_redraw(void)
{
	switch (rs) {
	case RS_READY: draw_ready(); break;
	case RS_REC: draw_rec(); break;
	case RS_PAUSED: draw_paused(); break;
	case RS_SAVED: draw_saved(); break;
	default: draw_error(); break;
	}
}
