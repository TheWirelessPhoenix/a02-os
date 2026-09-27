#ifndef A02_KEYS_H
#define A02_KEYS_H
#include "hw.h"

enum { KEY_NONE, KEY_NEXT, KEY_PREV, KEY_MENU, KEY_DOWN, KEY_BACK, KEY_PLAY };

void delay(uint32_t units);
void keys_init(void);
int keys_poll(void); /* returns a key once per press */

#endif
