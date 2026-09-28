#ifndef A02_SD_H
#define A02_SD_H
#include "lcd.h"

extern uint32_t sd_sectors;
extern uint32_t sd_last_stat;
int sd_init(uint32_t *dbg);
int sd_read(uint32_t lba, uint32_t count, void *buf);
int sd_write(uint32_t lba, uint32_t count, const void *buf);
int sd_wait_ready(void);

#endif
