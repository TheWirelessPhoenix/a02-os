#ifndef A02_LCD_H
#define A02_LCD_H
#include "hw.h"
void delay(uint32_t units);
uint32_t clock_ms(void); /* monotonic ms, true even while drawing (see lcd.c) */
void lcd_slow_account(uint32_t saved_cmu, uint32_t t0); /* after a 24 MHz pixel burst */
void lcd_init(void);
void lcd_fill_pattern(void);
void lcd_rect(int x, int y, int w, int h, uint16_t c);
void backlight_on(uint8_t level);
void lcd_window(int x0, int y0, int x1, int y1);
void lcd_lock(void);
void lcd_unlock(void);
uint32_t px(uint16_t c);
void lcd_char(int x, int y, char ch, int scale, uint16_t fg, uint16_t bg);
void lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);
#endif
