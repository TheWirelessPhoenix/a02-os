/* Buttons: resistor ladder on PMU+0x64 (verified levels) + Play flag PMU+0x2c bit1 (read-only). */
#include "keys.h"
#include "lcd.h" /* clock_ms() */

#define PMU_BASE 0xc0010000
#define LADDER   REG(PMU_BASE + 0x64)
#define PMU_KEY  REG(PMU_BASE + 0x2c)

#define DEMCR      REG(0xe000edfc)
#define DWT_CTRL   REG(0xe0001000)
#define LONG_MS    2000u /* hold time for KEY_POWER / to cancel a Play press */

static int held, play_prev, back_down, back_long;
static uint32_t back_t0, play_t0;

void keys_init(void)
{
	PAD_CFG(17) = 0x303b;
	CMU_DEVCLKEN |= 0x8000000;
	REG(CMU_BASE + 0x7c) = 0;
	REG(PMU_BASE + 0x38) |= 0x200 | 1;
	play_prev = (PMU_KEY & 2) != 0;
	DEMCR |= 1u << 24; /* cycle counter: long-press timing */
	DWT_CTRL |= 1;
	play_t0 = clock_ms() - LONG_MS - 1; /* Play already held at start: its release is not a press */
}

/* Back: report on release (short) or once after ~2 s held (KEY_POWER). */
static int back_key(int down)
{
	if (down) {
		if (!back_down) {
			back_down = 1;
			back_long = 0;
			back_t0 = clock_ms();
		} else if (!back_long && clock_ms() - back_t0 > LONG_MS) {
			back_long = 1;
			return KEY_POWER;
		}
		return KEY_NONE;
	}
	if (back_down) {
		back_down = 0;
		if (!back_long)
			return KEY_BACK;
	}
	return KEY_NONE;
}

static int ladder_key(uint32_t v)
{
	if (v < 70)  return KEY_NEXT;
	if (v < 210) return KEY_PREV;
	if (v < 350) return KEY_MENU;
	if (v < 490) return KEY_DOWN;
	if (v < 630) return KEY_BACK;
	return KEY_NONE;
}

int keys_poll(void)
{
	int k = ladder_key(LADDER & 0x3ff);
	if (k == KEY_BACK && !back_down && !held) {
		delay(1); /* other keys sweep through the Back band on the way down: confirm */
		k = ladder_key(LADDER & 0x3ff);
	}
	if ((k == KEY_BACK && !held) || back_down) {
		int r = back_key(k == KEY_BACK);
		if (r != KEY_NONE || k == KEY_BACK)
			return r;
		k = ladder_key(LADDER & 0x3ff); /* Back just released: look at the rest normally */
	}
	if (k != KEY_NONE) {
		if (held)
			return KEY_NONE;
		delay(1); /* settle, then confirm */
		k = ladder_key(LADDER & 0x3ff);
		if (k == KEY_NONE)
			return KEY_NONE;
		held = 1;
		return k;
	}
	held = 0;
	/* Play fires on release of a short press: holding it (10 s = reboot into USB recovery,
	 * power.c) must not also pause/select on the way. */
	int p = (PMU_KEY & 2) != 0;
	if (p && !play_prev) {
		play_t0 = clock_ms();
	} else if (!p && play_prev) {
		play_prev = p;
		return clock_ms() - play_t0 < LONG_MS ? KEY_PLAY : KEY_NONE;
	}
	play_prev = p;
	return KEY_NONE;
}
