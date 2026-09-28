/* Buttons: resistor ladder on PMU+0x64 (verified levels) + Play flag PMU+0x2c bit1 (read-only). */
#include "keys.h"

#define PMU_BASE 0xc0010000
#define LADDER   REG(PMU_BASE + 0x64)
#define PMU_KEY  REG(PMU_BASE + 0x2c)

#define DEMCR      REG(0xe000edfc)
#define DWT_CTRL   REG(0xe0001000)
#define DWT_CYCCNT REG(0xe0001004)
#define LONG_CYCLES (2u * 96000000u) /* ~2 s at 96 MHz */

static int held, play_prev, back_down, back_long;
static uint32_t back_t0;

void keys_init(void)
{
	PAD_CFG(17) = 0x303b;
	CMU_DEVCLKEN |= 0x8000000;
	REG(CMU_BASE + 0x7c) = 0;
	REG(PMU_BASE + 0x38) |= 0x200 | 1;
	play_prev = (PMU_KEY & 2) != 0;
	DEMCR |= 1u << 24; /* cycle counter: long-press timing */
	DWT_CTRL |= 1;
}

/* Back: report on release (short) or once after ~2 s held (KEY_POWER). */
static int back_key(int down)
{
	if (down) {
		if (!back_down) {
			back_down = 1;
			back_long = 0;
			back_t0 = DWT_CYCCNT;
		} else if (!back_long && DWT_CYCCNT - back_t0 > LONG_CYCLES) {
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
	int p = (PMU_KEY & 2) != 0;
	if (p != play_prev) {
		play_prev = p;
		if (p)
			return KEY_PLAY;
	}
	return KEY_NONE;
}
