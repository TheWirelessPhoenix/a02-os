#ifndef A02_KEYS_H
#define A02_KEYS_H
#include "hw.h"

/* KEY_BACK and KEY_PLAY fire on release of a short press (< ~2 s). Holding Back ~2 s fires
 * KEY_POWER once; a long Play hold fires nothing (10 s = reboot into USB recovery, power.c). */
enum { KEY_NONE, KEY_NEXT, KEY_PREV, KEY_MENU, KEY_DOWN, KEY_BACK, KEY_PLAY, KEY_POWER };

void delay(uint32_t units);
void keys_init(void);
int keys_poll(void); /* returns a key once per press */

#endif
