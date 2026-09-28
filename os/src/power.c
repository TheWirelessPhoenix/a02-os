/* Power off / reboot. Register sequences are copied from the stock apps (Ghidra):
 *  - ADFU reboot:  udisk.ap resident text 0x108ee0 (file 0x2ee0)
 *  - plain reboot: setting.ap file 0x7c5c / fwupdate.AP file 0x3b68
 *  - power off:    config.ap bank vaddr 0x1804134c (file 0x4350), shows end.sty first
 * The stock delays between power-off writes are sys_udelay(500) (syscall 0x1e = UDELAY). */
#include "power.h"
#include "hw.h"

#define PMU(o)       REG(0xc0010000 + (o))
#define PAD_MISC     REG(0xc01c0304) /* bit16 = USB/VBUS present (stock waits on it) */
#define ADFU_MAGIC   REG(0x130000)   /* checked by the boot ROM after a watchdog reset */
#define DWT_CTRL     REG(0xe0001000)
#define DWT_CYCCNT   REG(0xe0001004)
#define DEMCR        REG(0xe000edfc)
#define LADDER       REG(0xc0010064)

static void udelay_approx(uint32_t us)
{
	/* ~0x1400 spins = 1.17 ms at 48 MHz (delay()); generous at 96 MHz */
	for (volatile uint32_t i = 0; i < us * 10; i++)
		;
}

static void quiesce(void)
{
	__asm__ volatile("cpsid i");
	REG(0xc0070300 + 4) &= ~1u; /* DAC DMA channel 2 off (0xc0070100 + 2 * 0x100) */
	PAD_CFG(23) |= 0x40;        /* speaker gate low (GPIO23) */
	REG(0xc01c0200) &= ~(1u << 23);
}

void power_reboot_adfu(void)
{
	quiesce();
	ADFU_MAGIC = 0xadf0adf0u;
	WD_CTL = 0x5f;
	for (;;)
		;
}

void power_reboot_stock(void)
{
	quiesce();
	WD_CTL = (WD_CTL & ~0x2eu) | 0x11;
	for (;;)
		;
}

void power_off(void)
{
	quiesce();
	PMU(0x2c) = (PMU(0x2c) & 0xfffffe3fu) | 0x80;
	udelay_approx(500);
	uint32_t v = PAD_MISC & 0xff1fffffu;
	if (!(PAD_MISC & 0x10000))
		v |= 0x600000;
	PAD_MISC = v;
	PMU(0x24) = 0x815;
	udelay_approx(500);
	PMU(0x28) = 0x01f00e1fu;
	udelay_approx(500);
	PMU(0x20) &= ~2u;
	udelay_approx(500);
	PMU(0x20) &= ~1u; /* power enable off: on battery the board dies here */
	for (int i = 0; i < 400; i++) { /* ~0.5 s for the rails to collapse */
		WD_CTL |= 1;
		udelay_approx(1000);
	}
	__asm__ volatile("cpsie i");
	/* still alive: running from USB. It powers off when USB is unplugged. */
}

/* ---- emergency escape ----
 * Polled from wdt_feed(), which every wait loop calls, so these work in most freezes:
 *   Back held ~6 s  or  Play/Pause held ~10 s  ->  reboot into USB recovery (ADFU).
 * A short Play press still toggles pause as usual. */

#define PMU_KEY REG(0xc001002c) /* bit1 = Play/Pause (power) key */

struct hold { uint32_t since, count; };
static struct hold back_h, play_h;
static int dwt_ok = -1;

/* 1 once `down` has been continuously true for `secs` (cycle counter; ~1 feed/ms fallback) */
static int held_for(struct hold *h, int down, uint32_t secs)
{
	if (!down) {
		h->since = h->count = 0;
		return 0;
	}
	uint32_t now = DWT_CYCCNT;
	if (now)
		dwt_ok = 1;
	if (dwt_ok) {
		if (!h->since)
			h->since = now | 1;
		return now - h->since > secs * 96000000u; /* at 96 MHz (double at 48) */
	}
	return ++h->count > secs * 1700u;
}

void wdt_poll(void)
{
	if (dwt_ok < 0) {
		DEMCR |= 1u << 24;
		DWT_CYCCNT = 0;
		DWT_CTRL |= 1;
		dwt_ok = 0;
	}
	uint32_t v = LADDER & 0x3ff;
	if (held_for(&back_h, v >= 490 && v < 630, 6) || held_for(&play_h, (PMU_KEY & 2) != 0, 10))
		power_reboot_adfu();
}
