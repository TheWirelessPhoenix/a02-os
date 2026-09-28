/* A02 (ATJ2157) hardware registers — derived from stock welcome.bin / drv_lcd.drv.
 * See docs/HARDWARE.md for provenance. */
#ifndef HW_H
#define HW_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* reset / clock management */
#define RMU_CTL         REG(0xc0000000) /* bit0 DMA?, bit13 LCD, bit25 PWM: 1 = out of reset */
#define CMU_BASE        0xc0001000
#define CMU_CTL         REG(CMU_BASE + 0x00)
#define CMU_DEVCLKEN    REG(CMU_BASE + 0x08) /* bit0, bit13 LCD, bit25 PWM */
#define CMU_LCDCLK      REG(CMU_BASE + 0x34) /* [3:0] div-1, [9:8] source */
#define CMU_PWMCLK      REG(CMU_BASE + 0x60)

/* pads / GPIO */
#define PAD_CFG(pin)    REG(0xc01c0004 + 4 * (pin)) /* [4:0] function: 2 LCD, 0x10 PWM, 0x40 GPIO */
#define GPIO_OE(pin)    REG(0xc01c0210 + 4 * ((pin) >> 5))
#define GPIO_DAT(pin)   REG(0xc01c0220 + 4 * ((pin) >> 5))
#define GPIO_BIT(pin)   (1u << ((pin) & 31))

/* backlight PWM */
#define PWM_CTL         REG(0xc0190400)
#define PWM_PERIOD      REG(0xc0190414)
#define PWM_DUTY        REG(0xc0190418)

/* LCD controller (8080 8-bit bus) */
#define LCDC_CTL        REG(0xc01a0000)
#define LCDC_REG08      REG(0xc01a0008)
#define LCDC_REG0C      REG(0xc01a000c)
#define LCDC_REG10      REG(0xc01a0010)
#define LCDC_FIFO       REG(0xc01a0014)

/* RTC block (unlock key 0xa596 -> 0x5a69); watchdog = RTC+0x1c (GL5110 WD_CTL layout) */
#define RTC_BASE        0xc0030000
#define WD_CTL          REG(RTC_BASE + 0x1c) /* bit0 CLR (feed), bit4 WDEN */

/* wdt_poll(): optional emergency-escape hook (power.c, hold Back ~6 s). Weak, so payloads
 * that do not link power.c get a null symbol and skip it. */
void wdt_poll(void) __attribute__((weak));
/* Core cycle counter (enabled by keys_init()); ~96 per microsecond at 96 MHz. */
static inline uint32_t cycles(void) { return *(volatile uint32_t *)0xe0001004; }
#define CYCLES_PER_MS 96000u

static inline void wdt_feed(void)
{
	WD_CTL |= 1;
	if (wdt_poll)
		wdt_poll();
}

/* CPU clock (kernel.drv FUN_0011a4d2): CMU+0x00 [1:0] source 1=HOSC 24M, 2=DEVPLL, 3=COREPLL;
 * [5:4] divider log2. PLL regs: 0xc0000110 COREPLL, 0xc0000114 DEVPLL, [6:0]*6 MHz, bit7 enable.
 * ADFU leaves the CPU on DEVPLL 48 MHz. */
#define COREPLL         REG(0xc0000110)
static inline void cpu_clock_mhz(uint32_t mhz)
{
	COREPLL = (COREPLL & ~0x7fu) | 0x80 | (mhz / 6);
	for (volatile int i = 0; i < 20000; i++)
		;
	REG(0xc0001000) = (REG(0xc0001000) & ~0x33u) | 3;
}

/* board pins (stock config.txt) */
#define PIN_LCD_RST     21
#define PIN_LCD_RS      8
#define PIN_LCD_CE      43
#define PIN_LCD_RD      10
#define PIN_LCD_WR      11
#define PIN_BACKLIGHT   22

#define LCD_W 128
#define LCD_H 160

#endif
