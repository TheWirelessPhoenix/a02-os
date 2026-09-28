/* Power off / reboot (sequences copied from stock firmware; see docs/HARDWARE.md "Power"). */
#ifndef A02_POWER_H
#define A02_POWER_H

/* Reboot into the boot ROM's USB recovery mode (ADFU, 10d6:10d6), ready for `make run`.
 * Stock udisk.ap adfu_reboot: RAM[0x130000] = 0xADF0ADF0, then WD_CTL = 0x5F. */
void power_reboot_adfu(void) __attribute__((noreturn));
/* Plain watchdog reboot: boots the stock firmware from flash (stock setting/fwupdate). */
void power_reboot_stock(void) __attribute__((noreturn));
/* Stock config.ap power-off: cuts the PMU enable. Returns only if the board is still
 * running afterwards, i.e. on USB power: it then goes off when USB is unplugged. */
void power_off(void);

/* Emergency escape, polled from wdt_feed(): Back held ~6 s or Play held ~10 s anywhere ->
 * power_reboot_adfu(). */
void wdt_poll(void);

#endif
