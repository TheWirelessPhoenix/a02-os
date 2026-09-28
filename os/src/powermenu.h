#ifndef A02_POWERMENU_H
#define A02_POWERMENU_H

/* Modal POWER screen (hold Back ~2 s). Returns 0 on cancel: the caller must redraw.
 * Power off / restart / reload do not return (except power off on USB power, which waits). */
int powermenu(void);

#endif
