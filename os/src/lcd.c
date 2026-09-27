/* LCD + backlight driver for the A02 (from stock welcome.bin). */
#include "lcd.h"

void delay(uint32_t units)
{
	/* one unit ~= 1.17 ms in ADFU (measured with dcal.c) */
	for (uint32_t u = 0; u < units; u++) {
		wdt_feed();
		for (volatile uint32_t i = 0; i < 0x1400; i++)
			;
	}
}

void lcd_lock(void)   { LCDC_CTL &= ~0x10000000u; }
void lcd_unlock(void) { LCDC_CTL |= 0x10000000u; }

/* The LCD FIFO has no flow control we know of: a read-back after each write paces the CPU so
 * back-to-back pixels are never dropped (an unrolled -O2 fill loop overran it: striped screens). */
static inline void fifo_write(uint32_t v)
{
	LCDC_FIFO = v;
	(void)LCDC_CTL;
}

static void lcd_cmd(uint8_t c)
{
	LCDC_CTL = (LCDC_CTL & 0xe7ffff3fu) | 1;
	fifo_write(c);
	delay(1);
}

static void lcd_data8(const uint8_t *p, int n)
{
	LCDC_CTL = (LCDC_CTL & 0xe7efff3fu) | 0x100040;
	for (int i = 0; i < n; i++) {
		LCDC_REG10 = 0;
		fifo_write(p[i]);
	}
}

static void lcd_pins(void)
{
	static const uint8_t bus[] = { 0, 1, 2, 3, 4, 5, 6, 7, PIN_LCD_RS, PIN_LCD_RD, PIN_LCD_WR };
	for (unsigned i = 0; i < sizeof(bus); i++)
		PAD_CFG(bus[i]) = (PAD_CFG(bus[i]) & ~0x1fu) + 2;
	PAD_CFG(PIN_LCD_CE) = (((PAD_CFG(PIN_LCD_CE) & ~0x1fu) + 2) & 0xffff8fffu) | 0x2000;
}

static void lcd_clocks(void)
{
	CMU_LCDCLK = (CMU_LCDCLK & 0xfffffcf0u) | 3;
	CMU_DEVCLKEN |= 0x2000;
	CMU_DEVCLKEN |= 1;
	RMU_CTL = (RMU_CTL & ~0x2000u) | 0x2000;
	LCDC_REG08 = 0;
	LCDC_REG0C = 0;
	LCDC_CTL = 1;
}

static void lcd_reset(void)
{
	PAD_CFG(PIN_LCD_RST) = (PAD_CFG(PIN_LCD_RST) & 0xffffff60u) | 0x40;
	GPIO_OE(PIN_LCD_RST) |= GPIO_BIT(PIN_LCD_RST);
	delay(10);
	GPIO_DAT(PIN_LCD_RST) |= GPIO_BIT(PIN_LCD_RST);
	delay(20);
	GPIO_OE(PIN_LCD_RST) |= GPIO_BIT(PIN_LCD_RST);
	delay(60);
}

/* 0xFD = command, 0xFE = delay, 0xFF = end (stock drv_lcd init table) */
static const uint8_t init_seq[] = {
	0xfd, 0x11, 0xfe, 0xb4, 0xfd, 0xf0, 0x11, 0xfd, 0xf4, 0xca, 0x78, 0x64,
	0xfd, 0xb1, 0x01, 0x08, 0x05, 0xfd, 0xb2, 0x01, 0x08, 0x05,
	0xfd, 0xb3, 0x01, 0x08, 0x05, 0x01, 0x08, 0x05, 0xfd, 0xb4, 0x03,
	0xfd, 0xc0, 0x28, 0x08, 0x04, 0xfd, 0xc1, 0xc0, 0xfd, 0xc2, 0x0d, 0x00,
	0xfd, 0xc3, 0x8d, 0x2a, 0xfd, 0xc4, 0x8d, 0xee, 0xfd, 0xc5, 0x07, 0xfd, 0x36, 0xc8,
	0xfd, 0xe0, 0x04, 0x22, 0x07, 0x0a, 0x2e, 0x30, 0x25, 0x2a, 0x28, 0x26, 0x2e, 0x3a, 0x00, 0x01, 0x03, 0x13,
	0xfd, 0xe1, 0x04, 0x16, 0x06, 0x0d, 0x2d, 0x26, 0x23, 0x27, 0x27, 0x25, 0x2d, 0x3b, 0x00, 0x01, 0x04, 0x13,
	0xfd, 0x3a, 0x05, 0xfd, 0x29, 0xff,
};

static void lcd_init_panel(void)
{
	lcd_lock();
	for (unsigned i = 0; i < sizeof(init_seq) && init_seq[i] != 0xff; i++) {
		if (init_seq[i] == 0xfd)
			lcd_cmd(init_seq[++i]);
		else if (init_seq[i] == 0xfe)
			delay(init_seq[++i]);
		else
			lcd_data8(&init_seq[i], 1);
	}
	lcd_unlock();
}

void lcd_window(int x0, int y0, int x1, int y1)
{
	uint8_t b[4] = { 0, (uint8_t)x0, 0, (uint8_t)x1 };
	lcd_cmd(0x2a);
	lcd_data8(b, 4);
	b[1] = (uint8_t)y0;
	b[3] = (uint8_t)y1;
	lcd_cmd(0x2b);
	lcd_data8(b, 4);
	lcd_cmd(0x2c);
}

/* RGB565 -> LCDC 16-bit FIFO format (r5<<19 | g6<<10 | b5<<3) */
uint32_t px(uint16_t c)
{
	return ((uint32_t)(c >> 11) << 19) | ((uint32_t)((c >> 5) & 0x3f) << 10) | ((uint32_t)(c & 0x1f) << 3);
}

void lcd_fill_pattern(void)
{
	static const uint16_t bars[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };
	uint32_t saved;

	lcd_lock();
	lcd_window(0, 0, LCD_W - 1, LCD_H - 1);
	saved = CMU_CTL;
	CMU_CTL = (saved & 0xfffffeccu) | 1;
	LCDC_CTL = (LCDC_CTL & 0xe7cfff37u) | 0x40;
	for (int y = 0; y < LCD_H; y++) {
		for (int x = 0; x < LCD_W; x++) {
			uint16_t c;
			if (y < 100)
				c = bars[x / 16];
			else if (y < 130)
				c = (uint16_t)(((x >> 2) << 11) | ((x >> 1) << 5) | (x >> 2)); /* gray ramp */
			else
				c = ((x ^ y) & 8) ? 0x2104 : 0x5aeb; /* checkerboard */
			fifo_write(px(c));
		}
	}
	CMU_CTL = saved;
	lcd_unlock();
}

void backlight_on(uint8_t level)
{
	CMU_DEVCLKEN |= 0x2000000;
	RMU_CTL |= 0x2000000;
	CMU_PWMCLK |= 0x200;
	CMU_PWMCLK = (CMU_PWMCLK & 0xfffffe00u) | 0xf;
	PWM_PERIOD = (PWM_PERIOD & 0xffff0000u) | 0x10;
	PWM_DUTY = (PWM_DUTY & 0xffff0000u) | level;
	PWM_CTL = (PWM_CTL & 0xfffffffcu) | 0x1c;
	delay(10);
	PAD_CFG(PIN_BACKLIGHT) = (PAD_CFG(PIN_BACKLIGHT) & 0xffffff20u) | 0x10;
}


void lcd_init(void)
{
	lcd_clocks();
	lcd_reset();
	lcd_pins();
	lcd_init_panel();
}

/* fill rectangle; caller must not hold lcd_lock */
void lcd_rect(int x, int y, int w, int h, uint16_t c)
{
	uint32_t saved, v = px(c);
	lcd_lock();
	lcd_window(x, y, x + w - 1, y + h - 1);
	saved = CMU_CTL;
	CMU_CTL = (saved & 0xfffffeccu) | 1;
	LCDC_CTL = (LCDC_CTL & 0xe7cfff37u) | 0x40;
	for (int i = 0; i < w * h; i++)
		fifo_write(v);
	CMU_CTL = saved;
	lcd_unlock();
}
