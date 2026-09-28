/* A02-OS look: themes + common widgets (design: "A02-OS UI Design" canvas, Paper + Pixel chosen). */
#include <string.h>
#include "ui.h"

#define RGB(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))

const struct theme themes[NTHEMES] = {
	{ "PAPER",    RGB(0xf3, 0xee, 0xe4), RGB(0xe3, 0xda, 0xc9), RGB(0x1f, 0x1b, 0x16), RGB(0x6f, 0x65, 0x56),
	  RGB(0xb8, 0x43, 0x0a), RGB(0x1f, 0x1b, 0x16), RGB(0xf3, 0xee, 0xe4), RGB(0xd6, 0xcc, 0xb9), RGB(0xc2, 0x1f, 0x1f) },
	{ "MIDNIGHT", RGB(0x0b, 0x0f, 0x14), RGB(0x17, 0x20, 0x2b), RGB(0xe8, 0xee, 0xf5), RGB(0x7d, 0x8a, 0x99),
	  RGB(0x3e, 0xc6, 0xff), RGB(0x1e, 0x3a, 0x52), RGB(0xff, 0xff, 0xff), RGB(0x1c, 0x26, 0x31), RGB(0xff, 0x4d, 0x4d) },
	{ "NEON",     RGB(0x11, 0x0a, 0x1d), RGB(0x21, 0x13, 0x38), RGB(0xf4, 0xea, 0xff), RGB(0x9a, 0x86, 0xb8),
	  RGB(0xff, 0x4f, 0xd8), RGB(0x3b, 0x1d, 0x63), RGB(0xff, 0xff, 0xff), RGB(0x26, 0x17, 0x3d), RGB(0x2e, 0xe6, 0xd6) },
	{ "TERMINAL", RGB(0x02, 0x08, 0x03), RGB(0x0b, 0x1d, 0x0e), RGB(0x7d, 0xff, 0x8a), RGB(0x3c, 0x8a, 0x46),
	  RGB(0xc4, 0xff, 0x5c), RGB(0x7d, 0xff, 0x8a), RGB(0x02, 0x08, 0x03), RGB(0x0e, 0x2a, 0x12), RGB(0xff, 0x5c, 0x5c) },
	{ "SUNSET",   RGB(0x1b, 0x0f, 0x0b), RGB(0x2e, 0x1a, 0x12), RGB(0xff, 0xea, 0xda), RGB(0xb0, 0x8a, 0x72),
	  RGB(0xff, 0x8a, 0x3d), RGB(0x5a, 0x2c, 0x17), RGB(0xff, 0xf4, 0xea), RGB(0x33, 0x20, 0x1a), RGB(0xff, 0xd2, 0x3d) },
};

const struct theme *T = &themes[0];
int theme_idx;

void ui_set_theme(int idx)
{
	theme_idx = idx;
	T = &themes[idx];
}

void num_str(char *s, uint32_t v)
{
	char t[12];
	int n = 0;
	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	for (int i = 0; i < n; i++)
		s[i] = t[n - 1 - i];
	s[n] = 0;
}

void time_str(char *s, uint32_t secs)
{
	char b[12];
	num_str(b, secs / 60);
	size_t n = strlen(b);
	memcpy(s, b, n);
	s[n++] = ':';
	s[n++] = (char)('0' + (secs % 60) / 10);
	s[n++] = (char)('0' + secs % 10);
	s[n] = 0;
}

/* draw at most maxch characters; ellipsis-free hard cut keeps it fast */
void ui_text_fit(int x, int y, const char *s, int maxch, uint16_t fg, uint16_t bg)
{
	char b[32];
	int n = 0;
	if (maxch > 31)
		maxch = 31;
	while (s[n] && n < maxch) {
		b[n] = s[n];
		n++;
	}
	b[n] = 0;
	lcd_text(x, y, b, 1, fg, bg);
}

/* Battery: PMU+0x3c ADC (10-bit). Levels from the stock firmware's table (shared app code,
 * music.ap 0x18da1fa6): level i when the 7-sample average >= tab[i]. Calibration not yet
 * checked against a real voltage: Settings > BATTERY shows the raw value. */
static const uint16_t bat_tab[6] = { 0, 706, 749, 768, 780, 821 };
static int bat_lvl = -1;

uint32_t battery_raw(void)
{
	uint32_t sum = 0;
	for (int i = 0; i < 7; i++)
		sum += REG(0xc0010000 + 0x3c) & 0x3ff;
	return (sum + 3) / 7;
}

int battery_level(void)
{
	uint32_t v = battery_raw();
	int l = 5;
	while (l > 0 && v < bat_tab[l])
		l--;
	/* hysteresis: only step up once clearly above the next threshold */
	if (bat_lvl >= 0 && l > bat_lvl && v < bat_tab[l] + 8u)
		l = bat_lvl;
	bat_lvl = l;
	return l;
}

static void battery(int x, int y)
{
	int l = battery_level();
	uint16_t c = l <= 1 ? T->rec : T->dim;
	int fill = l * 7 / 5;
	lcd_rect(x, y, 11, 1, c);
	lcd_rect(x, y + 6, 11, 1, c);
	lcd_rect(x, y, 1, 7, c);
	lcd_rect(x + 10, y, 1, 7, c);
	lcd_rect(x + 11, y + 2, 1, 3, c);
	lcd_rect(x + 2, y + 2, fill, 3, c);
	lcd_rect(x + 2 + fill, y + 2, 7 - fill, 3, T->bg);
}

/* USB/VBUS present: bit16 of 0xc01c0304 (stock power-off and charge code test it) */
int usb_powered(void)
{
	return (REG(0xc01c0304) & 0x10000) != 0;
}

/* ---- Volume pill (mockup style B) ----
 * Right margin, over plain background only (titles stop at x 119), so hiding it is just redrawing
 * its ~400 pixels in the background color (pill_draw(0)); nothing else is repainted. pill_draw(k)
 * can also blend toward the background (k = opacity) for a future fade. */
#define PILL_X  (LCD_W - 6)
#define PILL_W  5
#define PILL_BH (VOLPILL_H - 12) /* bar height; speaker icon below */
static int pill_on, pill_level, pill_max = 1;
static uint32_t pill_t, pill_acc; /* shown at clock_ms() pill_t; pill_acc = backup ms count */

static uint16_t mix565(uint16_t a, uint16_t b, int k) /* k/256 of b over a */
{
	int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
	int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
	return (uint16_t)(((ar + ((br - ar) * k >> 8)) << 11) | ((ag + ((bg - ag) * k >> 8)) << 5) |
			  (ab + ((bb - ab) * k >> 8)));
}

static void pill_draw(int k) /* k = opacity 0..256 */
{
	const int x = PILL_X, y = VOLPILL_Y, h = PILL_BH;
	uint16_t line = mix565(T->bg, T->line, k), surf = mix565(T->bg, T->surface, k);
	uint16_t acc = mix565(T->bg, T->accent, k), dim = mix565(T->bg, T->dim, k);
	int fh = (h - 2) * pill_level / pill_max;
	lcd_rect(x + 1, y, PILL_W - 2, 1, line);
	lcd_rect(x + 1, y + h - 1, PILL_W - 2, 1, line);
	lcd_rect(x, y + 1, 1, h - 2, line);
	lcd_rect(x + PILL_W - 1, y + 1, 1, h - 2, line);
	lcd_rect(x + 1, y + 1, PILL_W - 2, h - 2 - fh, surf);
	lcd_rect(x + 1, y + 1 + (h - 2 - fh), PILL_W - 2, fh, acc);
	int sx = x + PILL_W / 2 - 3, sy = y + h + 4; /* speaker */
	lcd_rect(sx, sy, 7, 7, T->bg);
	lcd_rect(sx, sy + 2, 2, 3, dim);
	lcd_rect(sx + 2, sy + 1, 1, 5, dim);
	lcd_rect(sx + 3, sy, 1, 7, dim);
	if (pill_level > 0)
		lcd_rect(sx + 5, sy + 2, 1, 3, dim);
}

/* Show / update at full color; it hides 1.5 s after the last call. */
void ui_vol_pill_show(int level, int max)
{
	pill_max = max > 0 ? max : 1;
	pill_level = level < 0 ? 0 : level > pill_max ? pill_max : level;
	pill_draw(256);
	pill_on = 1;
	pill_t = clock_ms();
	pill_acc = 0;
}

/* ms_hint: roughly how long since the previous call (backup timer if the clock ever stalls). */
void ui_vol_pill_tick(uint32_t ms_hint)
{
	if (!pill_on)
		return;
	pill_acc += ms_hint;
	if (clock_ms() - pill_t >= 1500 || pill_acc >= 1500) {
		pill_draw(0); /* opacity 0 = background: erased */
		pill_on = 0;
	}
}

void ui_vol_pill_reset(void) /* the screen was repainted: forget the pill without drawing */
{
	pill_on = 0;
}

void ui_battery_refresh(void)
{
	battery(LCD_W - 17, 3);
}

void ui_header(const char *title, int playing)
{
	lcd_rect(0, 0, LCD_W, HDR_H - 1, T->bg);
	lcd_text(5, 3, title, 1, T->accent, T->bg);
	if (playing)
		lcd_text(LCD_W - 26, 3, ">", 1, T->accent, T->bg);
	battery(LCD_W - 17, 3);
	lcd_rect(0, HDR_H - 1, LCD_W, 1, T->line);
}

/* Recorder header: same bar as ui_header() but with the red REC dot in front of the
 * title (recorder UI design), and the dot can be blinked off by on == 0. */
void ui_rec_header(const char *title, int on)
{
	lcd_rect(0, 0, LCD_W, HDR_H - 1, T->bg);
	if (on) {
		lcd_rect(5, 5, 5, 4, T->rec);
		lcd_rect(6, 4, 3, 6, T->rec);
		lcd_text(14, 3, title, 1, T->accent, T->bg);
	} else {
		lcd_text(5, 3, title, 1, T->accent, T->bg);
	}
	battery(LCD_W - 17, 3);
	lcd_rect(0, HDR_H - 1, LCD_W, 1, T->line);
}

void ui_hints(const char *left, const char *right)
{
	int y = LCD_H - HINT_H;
	lcd_rect(0, y, LCD_W, 1, T->line);
	lcd_rect(0, y + 1, LCD_W, HINT_H - 1, T->bg);
	lcd_text(5, y + 3, left, 1, T->dim, T->bg);
	lcd_text(LCD_W - 5 - 6 * (int)strlen(right), y + 3, right, 1, T->dim, T->bg);
}

void ui_body_clear(void)
{
	lcd_rect(0, HDR_H, LCD_W, LCD_H - HDR_H - HINT_H, T->bg);
}

void ui_row(int slot, char icon, const char *label, const char *meta, int selected)
{
	int y = ROW_Y0 + slot * ROW_H;
	uint16_t bg = selected ? T->sel : T->bg, fg = selected ? T->sel_text : T->text;
	int mlen = meta ? (int)strlen(meta) : 0;
	char ic[2] = { icon, 0 };

	lcd_rect(0, y, 3, ROW_H, T->bg);
	lcd_rect(LCD_W - 3, y, 3, ROW_H, T->bg);
	lcd_rect(3, y, LCD_W - 6, ROW_H, bg);
	lcd_text(7, y + 4, ic, 1, selected ? T->sel_text : T->accent, bg);
	ui_text_fit(17, y + 4, label, (LCD_W - 24 - (mlen ? mlen * 6 + 4 : 0)) / 6, fg, bg);
	if (mlen)
		lcd_text(LCD_W - 7 - mlen * 6, y + 4, meta, 1, selected ? T->sel_text : T->dim, bg);
}
