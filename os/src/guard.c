/* Speaker gate + jack guard hardware glue. The policy lives in route.c.
 * GPIO23 = speaker amp enable (stock music.ap timer handler; validated on device by spktest):
 * PAD_CFG(23) |= 0x40 (output enable), bit 23 of 0xc01c0200 (bank-0 output data) 1 = on. */
#include "guard.h"
#include "hw.h"

#define LADDER    REG(0xc0010064)
#define SPK_PIN   23
#define GPIO_OUT0 REG(0xc01c0200)

int jack_guard = 1;
static struct route rt;
static int gate = -1, latched;

static void spk_gate(int on)
{
	if (on == gate)
		return;
	PAD_CFG(SPK_PIN) |= 0x40;
	if (on)
		GPIO_OUT0 |= 1u << SPK_PIN;
	else
		GPIO_OUT0 &= ~(1u << SPK_PIN);
	gate = on;
}

void guard_init(void)
{
	route_reset(&rt);
	gate = -1;
	spk_gate(0);
	latched = ROUTE_EV_NONE;
}

int guard_poll(void)
{
	int e = route_sample(&rt, LADDER);
	spk_gate(route_gate(&rt));
	if (e != ROUTE_EV_NONE)
		latched = e;
	return e;
}

int guard_settle(void)
{
	for (int i = 0; i < 400 && (rt.jack == JACK_UNKNOWN || rt.cls == RC_OTHER || rt.run < ROUTE_CONFIRM); i++) {
		guard_poll();
		for (volatile int k = 0; k < 0x2000; k++) /* ~1 ms at 96 MHz */
			;
	}
	return rt.jack;
}

int guard_user_play(void)
{
	guard_settle();
	latched = ROUTE_EV_NONE; /* jack changes before this Play are already reflected in the state */
	route_arm(&rt);
	spk_gate(route_gate(&rt));
	return guard_audible();
}

int guard_audible(void)
{
	return route_audible(&rt);
}

int guard_take_event(void)
{
	int e = latched;
	latched = ROUTE_EV_NONE;
	return e;
}

/* Guard OFF = player behaves as before the guard: jack changes never pause. The speaker
 * interlock still applies (route_gate), so after an unplug playback continues unheard until
 * the user pauses and presses Play again on the empty jack. */
int guard_must_pause(void)
{
	if (!jack_guard)
		return 0;
	if (latched != ROUTE_EV_NONE)
		return 1;
	return !(rt.jack == JACK_PHONES || (rt.jack == JACK_EMPTY && rt.armed));
}

void guard_quiet(void)
{
	spk_gate(0);
}

void guard_off(void)
{
	rt.armed = 0;
	spk_gate(0);
}

int guard_jack(void)
{
	return rt.jack;
}
