/* Speaker gate (GPIO23, device-validated with spktest) + headphone jack guard. */
#ifndef A02_GUARD_H
#define A02_GUARD_H
#include "route.h"

extern int jack_guard; /* Settings "JACK GUARD": 1 (default) = pause on insert AND removal; 0 = never pause */

void guard_init(void);       /* gate off; call before audio_init() */
int guard_poll(void);        /* one ladder sample; drives the gate; returns ROUTE_EV_* */
int guard_settle(void);      /* poll until the jack settles (bounded); returns JACK_* */
int guard_user_play(void);   /* explicit user Play: arm the speaker if the jack is empty */
int guard_audible(void);     /* output may be heard now (phones in, or armed speaker) */
int guard_take_event(void);  /* latched ROUTE_EV_* since the last call (cleared) */
/* Guard ON: 1 = halt now (no audible route, or a jack event is pending). Guard OFF: always 0
 * (the speaker interlock still holds via the gate). */
int guard_must_pause(void);
void guard_quiet(void);      /* gate off, keep the arm (between auto-advanced tracks) */
void guard_off(void);        /* gate off and disarm (playback ended) */
int guard_jack(void);

#endif
