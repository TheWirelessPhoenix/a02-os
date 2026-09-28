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
void ui_rec_header(const char *title, int rec_dot_on);
void ui_hints(const char *left, const char *right);
void ui_body_clear(void);
void ui_row(int slot, char icon, const char *label, const char *meta, int selected);
void ui_text_fit(int x, int y, const char *s, int maxch, uint16_t fg, uint16_t bg);
void num_str(char *s, uint32_t v);
uint32_t battery_raw(void);   /* averaged PMU+0x3c battery ADC */
int battery_level(void);      /* 0 (empty) .. 5 (full) */
void ui_battery_refresh(void); /* redraw just the header battery icon */
#define VOLPILL_Y 26 /* volume pill: right margin x LCD_W-6..LCD_W-1, y VOLPILL_Y..+VOLPILL_H */
#define VOLPILL_H 84
void ui_vol_pill_show(int level, int max); /* show / update; fades out 1.5 s after the last call */
void ui_vol_pill_tick(uint32_t ms_hint);   /* hides it 1.5 s after the last show; call often */
void ui_vol_pill_reset(void);              /* screen repainted: pill gone */
int usb_powered(void);        /* 1 = on USB power (charging) */
void time_str(char *s, uint32_t secs);

#endif
