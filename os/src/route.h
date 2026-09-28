/* Headphone/speaker route state machine (pure logic, host-testable; see host/route_test.c).
 * Fed one raw PMU+0x64 ladder sample per poll (~1 ms apart). Fail-closed rules:
 *  - the speaker gate is on only if the user armed it while the jack was settled EMPTY and
 *    the last ROUTE_CONFIRM samples were all EMPTY; any other sample drops it immediately;
 *  - a settled change (EMPTY <-> PHONES, ROUTE_CONFIRM consecutive samples) disarms the speaker
 *    and is reported as an event; nothing ever re-arms except route_arm() (explicit user Play).
 * Key presses share the ladder: they drop the gate while held but never change the jack state. */
#ifndef A02_ROUTE_H
#define A02_ROUTE_H
#include <stdint.h>

#define ROUTE_JACK_THR 784 /* stock key.drv 0x310: below = headphones */
#define ROUTE_PHONES_MIN 640 /* above the highest key level (Back ~559) */
#define ROUTE_CONFIRM 16

enum { JACK_UNKNOWN, JACK_EMPTY, JACK_PHONES };
enum { RC_EMPTY, RC_PHONES, RC_OTHER };
enum { ROUTE_EV_NONE, ROUTE_EV_INSERTED, ROUTE_EV_REMOVED };

struct route {
	uint8_t jack;  /* settled JACK_* */
	uint8_t cls;   /* class of the current run */
	uint16_t run;  /* consecutive samples of cls (saturating) */
	uint8_t armed; /* speaker authorised by explicit user action */
};

void route_reset(struct route *r);
int route_class(uint32_t raw);
int route_sample(struct route *r, uint32_t raw); /* returns ROUTE_EV_* */
int route_gate(const struct route *r);          /* 1 = speaker amp may be on */
int route_arm(struct route *r);                  /* 1 if armed (jack settled EMPTY now) */
/* 1 if audio may be heard right now: phones settled, or speaker gate on */
int route_audible(const struct route *r);

#endif
