/* Lecture recorder engine: mic (ADC) capture -> 16-bit PCM WAV on the SD card.
 * UI-agnostic: the caller pumps rec_pump() and draws rec_secs()/rec_level().
 *
 * rec_start()/rec_pump()/rec_pause()/rec_resume() return 0 on success or a negative
 * error code; rec_error() reports the first code of the current session (sticky until
 * the next rec_start()). Persistent error codes are shared so the UI can tell the user
 * the truth instead of guessing:
 *
 *    -1  invalid state (not active, already paused, invalid rate)
 *    -2  cannot create the .WAV (card missing, bad folder, no space for a file entry)
 *    -3  could not write the placeholder header
 *    -4  ADC would not start at the requested rate
 *    -5  ADC DMA timeout during capture (mic/clock lost)
 *    -6  card full or write error while streaming PCM
 *    -7  finalize failed: header rewrite, sync or close did not complete
 *    -8  WAV size cap reached (data chunk would overflow the 32-bit RIFF field)
 *    -9  ADC would not reopen on resume (transient; does not poison the session)
 *   -10  file already exists (rec_start uses exclusive creation; never overwrites)
 *
 * rec_finalized() is 1 only when the last finalize() rewrote the header, synced and
 * closed successfully, so a caller can claim SAVED honestly. A lecture cut short by a
 * full card (or the size cap) is still finalized and therefore playable. */
#ifndef A02_RECORDER_H
#define A02_RECORDER_H
#include "hw.h"

#define REC_RATE   24000 /* known-good bring-up rate; tune upward after recorder is stable */
#define REC_AGAIN  10    /* mic analog gain 0..15 (stock default 10) */
#define REC_DGAIN  20    /* ADC digital gain 0..59; 40 clipped speech on device */

/* Largest data-chunk size that keeps 36 + data inside the 32-bit RIFF field.
 * 0xFFFF_F000 leaves 4 KB of headroom; the recorded file stops there with -8.
 * The host engine test overrides this to a tiny value to exercise the cap. */
#ifndef REC_MAX_BYTES
#define REC_MAX_BYTES 0xFFFFF000u
#endif

int  rec_start(const char *path, uint32_t hz); /* 0 ok, <0 error; exclusive create */
int  rec_pump(void);                           /* call often; 0 ok, <0 error */
int  rec_pause(void);                          /* suspend capture, keep the file open */
int  rec_resume(void);                         /* continue the same file */
void rec_stop(void);                           /* finalize the header and close */

int      rec_active(void);
int      rec_paused(void);
int      rec_error(void);
int      rec_finalized(void); /* 1 if the last finalize completed (header+sync+close) */
uint32_t rec_pcm_bytes(void);
uint32_t rec_secs(void);
uint8_t  rec_level(void); /* peak of the last buffer, 0..255 */

#endif
