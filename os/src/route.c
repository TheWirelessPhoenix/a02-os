/* Headphone/speaker route state machine — see route.h. No hardware access here. */
#include "route.h"

void route_reset(struct route *r)
{
	r->jack = JACK_UNKNOWN;
	r->cls = RC_OTHER;
	r->run = 0;
	r->armed = 0;
}

int route_class(uint32_t raw)
{
	raw &= 0x3ff;
	if (raw >= ROUTE_JACK_THR)
		return RC_EMPTY;
	if (raw >= ROUTE_PHONES_MIN)
		return RC_PHONES;
	return RC_OTHER;
}

int route_sample(struct route *r, uint32_t raw)
{
	int c = route_class(raw);
	if (c == r->cls) {
		if (r->run < 0xffff)
			r->run++;
	} else {
		r->cls = (uint8_t)c;
		r->run = 1;
	}
	if (c == RC_OTHER || r->run < ROUTE_CONFIRM)
		return ROUTE_EV_NONE;
	int now = c == RC_EMPTY ? JACK_EMPTY : JACK_PHONES;
	if (now == r->jack)
		return ROUTE_EV_NONE;
	int was = r->jack;
	r->jack = (uint8_t)now;
	r->armed = 0;
	if (was == JACK_UNKNOWN)
		return ROUTE_EV_NONE; /* first settle is a state, not an event */
	return now == JACK_PHONES ? ROUTE_EV_INSERTED : ROUTE_EV_REMOVED;
}

int route_gate(const struct route *r)
{
	return r->armed && r->jack == JACK_EMPTY && r->cls == RC_EMPTY && r->run >= ROUTE_CONFIRM;
}

int route_arm(struct route *r)
{
	r->armed = r->jack == JACK_EMPTY && r->cls == RC_EMPTY && r->run >= ROUTE_CONFIRM;
	return r->armed;
}

int route_audible(const struct route *r)
{
	return r->jack == JACK_PHONES || route_gate(r);
}
