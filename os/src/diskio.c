/* FatFs disk glue: drive 0 = microSD. */
#include "ff.h"
#include "diskio.h"
#include "sd.h"
#include <string.h>

static int sd_ok;

DSTATUS disk_status(BYTE pdrv)
{
	return (pdrv == 0 && sd_ok) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
	static uint32_t dbg[8];
	if (pdrv != 0)
		return STA_NOINIT;
	if (!sd_ok)
		sd_ok = sd_init(dbg) == 0;
	return sd_ok ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != 0 || !sd_ok)
		return RES_NOTRDY;
	/* DMA needs a word-aligned destination */
	if ((uintptr_t)buff & 3) {
		static uint32_t bounce[128];
		for (UINT i = 0; i < count; i++) {
			if (sd_read((uint32_t)sector + i, 1, bounce))
				return RES_ERROR;
			memcpy(buff + i * 512, bounce, 512);
		}
		return RES_OK;
	}
	return sd_read((uint32_t)sector, count, buff) ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != 0 || !sd_ok)
		return RES_NOTRDY;
	/* DMA needs a word-aligned source */
	if ((uintptr_t)buff & 3) {
		static uint32_t bounce[128];
		for (UINT i = 0; i < count; i++) {
			memcpy(bounce, buff + i * 512, 512);
			if (sd_write((uint32_t)sector + i, 1, bounce))
				return RES_ERROR;
		}
		return RES_OK;
	}
	return sd_write((uint32_t)sector, count, buff) ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	(void)pdrv;
	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = sd_sectors;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1;
		return RES_OK;
	}
	return RES_PARERR;
}
