/* microSD driver for the A02 (ATJ2157 SD controller 0 @ 0xc0160000), derived from stock card.drv.
 * Block reads via DMA channel 4. Register usage documented in docs/HARDWARE.md. */
#include "sd.h"

#define SDC(o)      REG(0xc0160000 + (o))
#define SD_CTL      SDC(0x00) /* bit7 enable, bit6 data, [1:0] bus width (1 = 4-bit) */
#define SD_CMDCTL   SDC(0x04) /* [5:0] type, [23:16] timing, bit7 go/busy, 0xbf000000 data xfer */
#define SD_STAT     SDC(0x08) /* errors: 0x8017 */
#define SD_CMD      SDC(0x0c)
#define SD_ARG      SDC(0x10)
#define SD_RSP0     SDC(0x14)
#define SD_RSP1     SDC(0x18)
#define SD_RSP2     SDC(0x1c)
#define SD_RSP3     SDC(0x20)
#define SD_FIFO_ADDR 0xc0160028
#define SD_BLKSZ    SDC(0x2c)
#define SD_BLKCNT   SDC(0x30)
#define SD_CLKDIV   REG(CMU_BASE + 0x14)

#define DMA_G0      REG(0xc0070000)
#define DMA_G1      REG(0xc0070004)
#define DMA4(o)     REG(0xc0070100 + 4 * 0x100 + (o))

/* board pins (stock config.txt): CLK 30, CMD 32, D0 33, D1 29, D2 28, D3 27; drive CLK 5, CMD/DATA 3 */
static const uint8_t pin_clk = 30, pin_cmd = 32, pin_d[4] = { 33, 29, 28, 27 };

static uint8_t timing = 0x6b;
static uint16_t rca;
static int sdhc;
uint32_t sd_sectors;

static int sd_status(void)
{
	uint32_t s = SD_STAT;
	if (!(s & 0x8017))
		return 0;
	if (s & 0x8000) return 5;
	if (s & 0x10) return 4;
	if (s & 1) return 1;
	if (s & 4) return 3;
	return 2;
}

static int sd_wait_go(void)
{
	for (uint32_t i = 0; i < 2000000; i++) {
		if (!(SD_CMDCTL & 0x80))
			return 0;
		if ((i & 0xfff) == 0)
			wdt_feed();
	}
	return -1;
}

/* FUN_0011e308 */
static int sd_cmd(uint32_t cmd, uint32_t arg, uint32_t type)
{
	if ((type & 0xf) == 0)
		type |= 0x20;
	if ((type & 0xf) != 4 && (type & 0xf) != 6)
		type |= 0x40;
	SD_ARG = arg;
	SD_CMD = cmd;
	SD_CMDCTL = type | (uint32_t)timing << 16 | 0x80;
	if (sd_wait_go())
		return 9;
	return sd_status();
}

/* FUN_10820000: 74+ init clocks */
static void sd_init_clocks(void)
{
	SD_CMDCTL = 0x00af00e8;
	for (int i = 0; i < 3000 && (SD_CMDCTL & 0x80); i++) {
		wdt_feed();
		delay(1);
	}
}

/* FUN_1082006a: clocks, reset, pins, controller enable */
static void sd_hw_init(void)
{
	CMU_DEVCLKEN |= 1u << 2 | 1;
	RMU_CTL &= ~(1u << 2);
	delay(2);
	RMU_CTL |= 1u << 2;
	PAD_CFG(pin_clk) = 0x804 | 5u << 12;
	PAD_CFG(pin_cmd) = 0x804 | 3u << 12;
	for (int i = 0; i < 4; i++)
		PAD_CFG(pin_d[i]) = 0x804 | 3u << 12;
	SD_CLKDIV = 0x41; /* slow identification clock */
	SD_CTL |= 0x80;
	SD_CMDCTL = (SD_CMDCTL & 0xff00ffffu) | 0xaf0000;
	SD_CTL &= 0xfffffeffu; /* controller index 0 */
}

static int ocr_ready(void)
{
	uint32_t r = SD_RSP1;
	if (!(r & 0x80))
		return 0;
	if (r & 0x40)
		sdhc = 1;
	return 1;
}

static uint32_t card_state(void)
{
	if (sd_cmd(13, (uint32_t)rca << 16, 1))
		return 0xff;
	return (SD_RSP0 & 0x1fffff) >> 17;
}

/* DMA ch4: SD FIFO -> buf (FUN_0011e48c mode 0) */
static void dma_setup_read(void *buf, uint32_t len)
{
	DMA4(4) &= ~1u;
	DMA_G0 = 0x10;
	DMA_G0 = 0x100000;
	DMA_G1 &= ~0x10u;
	DMA_G1 &= ~0x100000u;
	DMA4(0x00) = 0x85; /* controller 0 read */
	DMA4(0x08) = SD_FIFO_ADDR;
	DMA4(0x10) = (uint32_t)buf;
	DMA4(0x18) = len;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
	uint32_t addr = sdhc ? lba : lba << 9;
	uint32_t cmd = count == 1 ? 17 : 18;
	int err;

	SD_CTL |= 0x40;
	dma_setup_read(buf, count * 512);
	SD_BLKCNT = count;
	SD_BLKSZ = 512;
	SD_ARG = addr;
	SD_CMD = cmd;
	SD_CMDCTL = 4 | (uint32_t)timing << 16 | 0xbf000000;
	DMA4(4) |= 1;
	SD_CMDCTL |= 0x80;
	if (sd_wait_go())
		return 9;
	if (!(SD_STAT & 0x8017)) {
		for (uint32_t i = 0; (DMA4(4) & 1); i++)
			if ((i & 0xfff) == 0)
				wdt_feed();
	} else {
		DMA4(4) &= ~1u;
	}
	err = sd_status();
	if (count > 1)
		sd_cmd(12, 0, 3);
	return err;
}

int sd_init(uint32_t *dbg)
{
	int err, tries;

	sd_hw_init();
	timing = 0x6b;

	/* FUN_08800954 / FUN_088008da: CMD0, CMD8, ACMD41 */
	sd_init_clocks();
	sd_cmd(0, 0, 0);
	sd_init_clocks();
	sd_cmd(8, 0x1aa, 1);
	dbg[1] = SD_RSP0;
	for (tries = 50; tries; tries--) {
		if ((err = sd_cmd(55, 0, 1)) != 0)
			break;
		if ((err = sd_cmd(41, 0x40ff8000, 0x21)) != 0)
			break;
		if (ocr_ready())
			break;
		delay(40);
	}
	dbg[2] = SD_RSP1;
	dbg[3] = (uint32_t)tries | (uint32_t)err << 8;
	if (!tries)
		return 6;
	if (err)
		return 10 + err;
	if ((err = sd_cmd(2, 0, 2)) != 0)
		return 20 + err;
	if ((err = sd_cmd(3, 0, 1)) != 0)
		return 30 + err;
	rca = (uint16_t)((SD_RSP0 >> 24) + (SD_RSP1 << 8));
	dbg[4] = rca;

	/* FUN_0880070c: CMD9 CSD -> capacity */
	if ((err = sd_cmd(9, (uint32_t)rca << 16, 2)) != 0)
		return 40 + err;
	if (sdhc)
		sd_sectors = ((SD_RSP1 >> 16 | (SD_RSP2 & 0x3f) << 16) + 1) * 1024;
	dbg[5] = sd_sectors;

	timing = 0x68;
	if ((err = sd_cmd(13, (uint32_t)rca << 16, 1)) != 0)
		return 50 + err;
	/* faster clock */
	SD_CLKDIV = (SD_CLKDIV & ~0x40u & ~0xfu) | 10;

	/* select card: CMD7 until transfer state (4) */
	if (card_state() != 4) {
		sd_cmd(7, (uint32_t)rca << 16, 3);
		if (card_state() != 4)
			return 60;
	}
	sd_cmd(16, 512, 1);

	/* ACMD6: 4-bit bus */
	if (sd_cmd(55, (uint32_t)rca << 16, 1) == 0 && sd_cmd(6, 2, 3) == 0)
		SD_CTL = (SD_CTL & 0xfffffffcu) | 1;
	return 0;
}
