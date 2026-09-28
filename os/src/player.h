#ifndef A02_PLAYER_H
#define A02_PLAYER_H
#include <stdint.h>

enum { PLAYER_DONE, PLAYER_NEXT, PLAYER_PREV, PLAYER_BACK, PLAYER_ERROR, PLAYER_CONTINUE };

struct player_ui {
	void (*on_start)(uint32_t hz, uint32_t channels, uint32_t kbps);
	void (*on_progress)(uint32_t pos, uint32_t total);
	void (*on_pause)(int paused);
	void (*on_key)(int key); /* MENU / DOWN while playing */
	void (*on_levels)(const uint8_t *lv, int n); /* NBANDS levels 0..255, every decoded frame */
	void (*on_tick)(void); /* called ~every ms while paused (UI timers); may be NULL */
};

#define NBANDS 12
extern char player_title[48], player_artist[40];
extern uint32_t player_secs, player_total_secs;
extern uint32_t player_frame_ms; /* duration of the last queued buffer (UI timers) */

int player_supported(const char *name);
int player_play(const char *path, struct player_ui *ui); /* returns PLAYER_* */

#endif
