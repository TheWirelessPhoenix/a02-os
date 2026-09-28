/* Recorder app: the five screens of the recorder UI design, driven by recorder.c.
 * Entry points are called from main.c's key loop; recapp_pump() runs every iteration. */
#ifndef A02_RECAPP_H
#define A02_RECAPP_H

void recapp_open(void); /* enter the recorder (from the home menu) */
int  recapp_pump(void); /* do periodic work; called every main-loop iteration */
int  recapp_key(int k); /* 1 = handled, stay in the app; 0 = leave to Home */
void recapp_redraw(void); /* repaint the current screen (after the POWER menu is canceled) */

#endif
