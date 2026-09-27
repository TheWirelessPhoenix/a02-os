#ifndef A02_UI_H
#define A02_UI_H
#include "lcd.h"

/* Screen metrics (device pixels; the mockup is drawn at 2x) */
#define HDR_H   14
#define HINT_H  12
#define ROW_H   15
#define ROW_Y0  (HDR_H + 2)
#define ROWS    ((LCD_H - HDR_H - HINT_H - 2) / ROW_H)

struct theme {
	const char *name;
	uint16_t bg, surface, text, dim, accent, sel, sel_text, line, rec;
};

#define NTHEMES 5
extern const struct theme themes[NTHEMES];
extern const struct theme *T;
extern int theme_idx;

void ui_set_theme(int idx);
void ui_header(const char *title, int playing);
void ui_hints(const char *left, const char *right);
void ui_body_clear(void);
void ui_row(int slot, char icon, const char *label, const char *meta, int selected);
void ui_text_fit(int x, int y, const char *s, int maxch, uint16_t fg, uint16_t bg);
void num_str(char *s, uint32_t v);
void time_str(char *s, uint32_t secs);

#endif
