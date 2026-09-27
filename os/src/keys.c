/* Buttons: resistor ladder on PMU+0x64 (verified levels) + Play flag PMU+0x2c bit1 (read-only). */
#include "keys.h"

#define PMU_BASE 0xc0010000
#define LADDER   REG(PMU_BASE + 0x64)
#define PMU_KEY  REG(PMU_BASE + 0x2c)

static int held, play_prev;

void keys_init(void)
{
	PAD_CFG(17) = 0x303b;
	CMU_DEVCLKEN |= 0x8000000;
	REG(CMU_BASE + 0x7c) = 0;
	REG(PMU_BASE + 0x38) |= 0x200 | 1;
	play_prev = (PMU_KEY & 2) != 0;
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
