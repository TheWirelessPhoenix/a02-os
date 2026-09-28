/* Host tests for src/route.c. Build + run: cc -O2 -Wall -Isrc host/route_test.c src/route.c -o /tmp/rt && /tmp/rt */
#include <stdio.h>
#include <stdlib.h>
#include "route.h"

#define EMPTY 836
#define EMPTY_PLAYING 811 /* measured while the DAC was running (spktest) */
#define PHONES 700
#define KEY_BACK 559
#define KEY_NEXT 0

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); fails++; } } while (0)

static int feed(struct route *r, uint32_t raw, int n, int *ev)
{
	int last = ROUTE_EV_NONE;
	for (int i = 0; i < n; i++) {
		int e = route_sample(r, raw);
		if (e != ROUTE_EV_NONE) {
			last = e;
			if (ev)
				(*ev)++;
		}
	}
	return last;
}

static void test_boot(void)
{
	struct route r;
	route_reset(&r);
	CHECK(!route_gate(&r) && !route_audible(&r), "gate closed at boot");
	CHECK(!route_arm(&r), "cannot arm while unknown");
	int ev = 0;
	feed(&r, EMPTY, ROUTE_CONFIRM - 1, &ev);
	CHECK(r.jack == JACK_UNKNOWN && !route_arm(&r), "not settled before ROUTE_CONFIRM");
	feed(&r, EMPTY, 1, &ev);
	CHECK(r.jack == JACK_EMPTY && ev == 0, "first settle is not an event");
	CHECK(!route_gate(&r), "settled empty alone does not open the gate");
	CHECK(route_arm(&r) && route_gate(&r), "explicit arm opens the gate");

	route_reset(&r);
	feed(&r, PHONES, ROUTE_CONFIRM, &ev);
	CHECK(r.jack == JACK_PHONES && ev == 0 && route_audible(&r) && !route_gate(&r), "boot with phones");
	CHECK(!route_arm(&r), "cannot arm with phones in");
}

static void test_insert_from_speaker(void)
{
	struct route r;
	route_reset(&r);
	feed(&r, EMPTY, ROUTE_CONFIRM, 0);
	route_arm(&r);
	route_sample(&r, PHONES);
	CHECK(!route_gate(&r), "gate drops on the FIRST raw phones sample");
	int e = feed(&r, PHONES, ROUTE_CONFIRM - 1, 0);
	CHECK(e == ROUTE_EV_INSERTED && r.jack == JACK_PHONES && !r.armed, "insert confirmed + disarmed");
	CHECK(route_audible(&r) && !route_gate(&r), "phones audible, speaker off");
}

static void test_remove_never_speaker(void)
{
	struct route r;
	route_reset(&r);
	feed(&r, PHONES, ROUTE_CONFIRM, 0);
	route_arm(&r); /* even a stray arm attempt while phones are in must not count */
	int ev = 0, e = ROUTE_EV_NONE;
	for (int i = 0; i < 1000; i++) {
		int x = route_sample(&r, EMPTY);
		if (x) {
			e = x;
			ev++;
		}
		CHECK(!route_gate(&r), "no speaker after removal");
	}
	CHECK(e == ROUTE_EV_REMOVED && ev == 1 && r.jack == JACK_EMPTY, "removal reported once");
	CHECK(!route_audible(&r), "nothing audible after removal until user arms");
	CHECK(route_arm(&r) && route_gate(&r), "user Play re-arms after removal");
}

static void test_keys_and_glitches(void)
{
	struct route r;
	route_reset(&r);
	feed(&r, EMPTY_PLAYING, ROUTE_CONFIRM, 0);
	route_arm(&r);
	int ev = 0;
	feed(&r, KEY_BACK, 300, &ev);
	CHECK(!route_gate(&r) && r.jack == JACK_EMPTY && ev == 0 && r.armed, "held key: gate off, no event");
	feed(&r, PHONES, 3, &ev); /* release transient through the phones band */
	CHECK(!route_gate(&r) && ev == 0, "short phones-band transient is not an insert");
	feed(&r, EMPTY_PLAYING, ROUTE_CONFIRM - 1, &ev);
	CHECK(!route_gate(&r), "gate waits for a full confirm run after a glitch");
	feed(&r, EMPTY_PLAYING, 1, &ev);
	CHECK(route_gate(&r) && ev == 0, "gate restored after the key (jack never changed)");

	/* phones plugged in while a key is held: seen once the key is released */
	feed(&r, KEY_NEXT, 50, &ev);
	int e = feed(&r, PHONES, ROUTE_CONFIRM, &ev);
	CHECK(e == ROUTE_EV_INSERTED && !route_gate(&r) && !r.armed, "insert behind a held key");
}

static void test_bounce(void)
{
	struct route r;
	route_reset(&r);
	feed(&r, EMPTY, ROUTE_CONFIRM, 0);
	route_arm(&r);
	int ev = 0;
	for (int i = 0; i < 200; i++) {
		route_sample(&r, (i & 1) ? EMPTY : PHONES);
		CHECK(!route_gate(&r), "bouncing contact keeps gate closed");
	}
	CHECK(ev == 0 && r.jack == JACK_EMPTY, "bounce alone does not settle");
	feed(&r, EMPTY, ROUTE_CONFIRM, &ev);
	CHECK(route_gate(&r), "armed survives bounce that settles back to empty");
	feed(&r, PHONES, ROUTE_CONFIRM, &ev);
	CHECK(ev == 1 && !r.armed, "bounce that settles to phones is one insert");
	feed(&r, EMPTY, ROUTE_CONFIRM, &ev);
	CHECK(ev == 2 && !route_gate(&r), "then removal: no speaker");
}

/* Random fuzz: invariants that must hold after every sample. */
static void test_fuzz(void)
{
	static const uint32_t pool[] = { 0, 140, 279, 420, 559, 630, 639, 640, 700, 783, 784, 811, 836, 900, 1023 };
	struct route r;
	srand(12345);
	for (int trial = 0; trial < 2000; trial++) {
		route_reset(&r);
		int empty_run = 0, arm_ok = 0, prev_jack = JACK_UNKNOWN;
		for (int i = 0; i < 4000; i++) {
			uint32_t v = (rand() % 4) ? pool[rand() % 15] : (uint32_t)(rand() % 1024);
			int n = 1 + (rand() % 40);
			for (int k = 0; k < n; k++) {
				int e = route_sample(&r, v);
				empty_run = v >= ROUTE_JACK_THR ? empty_run + 1 : 0;
				if (r.jack != prev_jack) {
					arm_ok = 0;
					if (prev_jack != JACK_UNKNOWN)
						CHECK(e != ROUTE_EV_NONE, "settled change reported");
					prev_jack = r.jack;
				} else {
					CHECK(e == ROUTE_EV_NONE, "no event without settled change");
				}
				if (route_gate(&r)) {
					CHECK(v >= ROUTE_JACK_THR, "gate only on an empty sample");
					CHECK(empty_run >= ROUTE_CONFIRM, "gate needs a full empty run");
					CHECK(arm_ok, "gate needs an arm since the last settled change");
					CHECK(r.jack == JACK_EMPTY, "gate only when settled empty");
				}
				if (fails > 20)
					return;
			}
			if (rand() % 50 == 0 && route_arm(&r))
				arm_ok = 1;
		}
	}
}

int main(void)
{
	test_boot();
	test_insert_from_speaker();
	test_remove_never_speaker();
	test_keys_and_glitches();
	test_bounce();
	test_fuzz();
	printf(fails ? "route_test: %d FAILED\n" : "route_test: all passed\n", fails);
	return fails != 0;
}
