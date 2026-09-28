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

static void battery(int x, int y)
{
	/* PMU+0x3c battery ADC (10-bit); ~0x2ab is the stock low-battery threshold */
	uint32_t v = REG(0xc0010000 + 0x3c) & 0x3ff;
	int fill = v <= 0x2b0 ? 1 : v >= 0x3c0 ? 7 : 1 + (int)(v - 0x2b0) * 6 / (0x3c0 - 0x2b0);
	lcd_rect(x, y, 11, 1, T->dim);
	lcd_rect(x, y + 6, 11, 1, T->dim);
	lcd_rect(x, y, 1, 7, T->dim);
	lcd_rect(x + 10, y, 1, 7, T->dim);
	lcd_rect(x + 11, y + 2, 1, 3, T->dim);
	lcd_rect(x + 2, y + 2, fill, 3, T->dim);
	lcd_rect(x + 2 + fill, y + 2, 7 - fill, 3, T->bg);
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
