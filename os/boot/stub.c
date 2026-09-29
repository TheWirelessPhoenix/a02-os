/* A02-OS boot stub. Replaces KER_INIT.BIN in the firmware image (LFI).
 *
 * Boot chain (docs/FLASH-INSTALL.md): boot ROM -> MBREC (0x101000) -> BREC
 * (0x10E900..0x119E08) loads KER_TEXT/KER_DATA/KER_INIT and calls KER_INIT at 0x108001 with
 * r0 = *(u32 *)0. We are that KER_INIT. The original KER_INIT rides along at 0x108800
 * (stub.ld) and is put back in place by the trampoline for the stock path.
 *
 * Play is the power button, so it is normally held when we start: we wait for its release.
 *   normal power-on            -> load A02OS.BIN from the LFI to 0x120000 and start it
 *   M/Vol/Next/Prev/Back held  -> stock firmware (original KER_INIT, as if we never ran)
 *   Play held ~6 s             -> USB recovery (ADFU), independent of both systems
 *   anything unexpected        -> stock firmware
 *
 * The button check runs before anything else, so the escape never depends on A02-OS.
 */
#include <stdint.h>

#define REG(a)        (*(volatile uint32_t *)(a))
#define PMU(o)        REG(0xc0010000 + (o))
#define LADDER        (PMU(0x64) & 0x3ff)   /* < 630: a ladder button is down (keys.c) */
#define PLAY_DOWN     (PMU(0x2c) & 2)       /* Play/Pause (keys.c, power.c) */
#define CMU_CTL       REG(0xc0001000)
#define CMU_DEVCLKEN  REG(0xc0001008)
#define PAD_CFG17     REG(0xc01c0048)
#define COREPLL       REG(0xc0000110)
#define DEVPLL        REG(0xc0000114)
#define WD_CTL        REG(0xc003001c)
#define ADFU_MAGIC    REG(0x130000)
#define DEMCR         REG(0xe000edfc)
#define DWT_CTRL      REG(0xe0001000)
#define DWT_CYCCNT    REG(0xe0001004)

/* BREC internals (brecf651, base 0x10E900) — checked before use */
#define BREC_READ     0x11485fu               /* read(sector, buf, count): NAND via BREC's FTL */
#define LFI_BASE      REG(0x20361c)           /* LFI byte offset, used by BREC's kernel loader */
static const uint8_t brec_read_sig[8] = {0x2d, 0xe9, 0xfc, 0x47, 0x15, 0x46, 0x89, 0x46};

#define A02OS_ADDR    0x120000u
#define A02OS_MAX     0x10000u                /* must end below the ADFU magic word at 0x130000 */
#define DIR_BUF       0x132000u               /* free while BREC runs; 0x1e00 bytes */

#ifdef STUB_TEST
/* RAM test build (boot/stubtest.c): record the decision instead of acting on it. */
#include "stubtest.h"
#define stock_boot()   do { stub_result(1); return; } while (0)
#define enter_a02os()  do { stub_result(2); return; } while (0)
#define adfu_reboot()  do { stub_result(3); return; } while (0)
#define STUB_NORETURN
#else
#define STUB_NORETURN __attribute__((noreturn))
#endif

/* Diagnostic build (make boot/stub-debug.bin): a report at 0x130100 that survives the reboot
 * into USB recovery, and every "go to stock" becomes "go to recovery" so the report can be read. */
struct dbg {
	uint32_t magic, stage, reason, hz, dwt_ok;
	uint32_t ladder0, play0, ladder1, play1, lfi_base, sig_ok;
	uint32_t a_sec, a_len, a_chk, a_sum;
	uint8_t dir0[32], sig[8];
};
#ifdef STUB_DEBUG
#define DBG ((volatile struct dbg *)0x130100)
#define D(field, v) (DBG->field = (uint32_t)(v))
#else
#define D(field, v) ((void)0)
#endif

typedef int (*brec_read_fn)(uint32_t sector, void *buf, uint32_t count, uint32_t unused);

#ifndef STUB_TEST
extern void stock_boot(void) __attribute__((noreturn));      /* stub_entry.s: trampoline */
extern void enter_a02os(void) __attribute__((noreturn));     /* stub_entry.s */
#endif

static uint32_t cpu_hz(void)
{
	uint32_t c = CMU_CTL, src = c & 3, div = 1u << ((c >> 4) & 3), mhz = 24;
	if (src == 2) mhz = (DEVPLL & 0x7f) * 6;
	else if (src == 3) mhz = (COREPLL & 0x7f) * 6;
	if (!mhz) mhz = 24;
	return mhz * 1000000u / div;
}

static int ladder_down(void) { return LADDER < 630; }
static int play_down(void) { return PLAY_DOWN != 0; }

/* The boot ROM leaves the watchdog running (seen in USB recovery: an unfed 2 s test reset
 * the player), so every wait loop feeds it. Feeding a stopped watchdog is harmless. */
static inline void wd_feed(void) { WD_CTL |= 1; }

/* Timing: the core cycle counter when it runs, and ALWAYS an iteration cap, so no wait can
 * hang (in USB recovery the counter did not advance and an uncapped wait froze the player).
 * One iteration costs well over 4 cycles, so the cap is only reached if the counter is dead. */
/* Time base: SysTick (core timer, counts CPU cycles down, 24 bit) — the DWT cycle counter does
 * not run on this chip in these states (measured: USB recovery). Loops poll far more often than
 * the 24-bit wrap (0.35 s at 48 MHz). The iteration cap stays as a backstop. */
#define SYST_CSR  REG(0xe000e010)
#define SYST_RVR  REG(0xe000e014)
#define SYST_CVR  REG(0xe000e018)

struct timing { uint32_t per_ms; int dwt_ok; };   /* no globals: the stub has no .data/.bss */
struct clock { uint32_t last, acc; };

static void clock_start(struct clock *c) { c->last = SYST_CVR; c->acc = 0; }

static uint32_t clock_cycles(struct clock *c)
{
	uint32_t now = SYST_CVR;
	c->acc += (c->last - now) & 0xffffff;             /* counts down */
	c->last = now;
	return c->acc;
}

static int elapsed(const struct timing *t, struct clock *c, uint32_t i, uint32_t ms)
{
	if (i >= ms * (t->per_ms / 4))
		return 1;
	return clock_cycles(c) >= ms * t->per_ms;
}

static void delay_ms(const struct timing *t, uint32_t ms)
{
	struct clock c;
	clock_start(&c);
	for (uint32_t i = 0; !elapsed(t, &c, i, ms); i++)
		wd_feed();
}

/* 1 if `down()` stays true for `ms`, sampling continuously */
static int held_for(const struct timing *t, int (*down)(void), uint32_t ms)
{
	struct clock c;
	clock_start(&c);
	for (uint32_t i = 0; !elapsed(t, &c, i, ms); i++) {
		wd_feed();
		if (!down())
			return 0;
	}
	return 1;
}

#ifndef STUB_TEST
static void __attribute__((noreturn)) adfu_reboot(void)
{
	__asm__ volatile("cpsid i");
	ADFU_MAGIC = 0xadf0adf0u;
	WD_CTL = 0x5f;
	for (;;)
		;
}
#endif

static uint32_t sum32(const uint32_t *p, uint32_t bytes)
{
	uint32_t s = 0;
	for (uint32_t i = 0; i < bytes / 4; i++)
		s += p[i];
	return s;
}

static int names_equal(const uint8_t *a, const char *b)
{
	for (int i = 0; i < 11; i++)
		if (a[i] != (uint8_t)b[i])
			return 0;
	return 1;
}

/* 0 = A02-OS loaded and verified at A02OS_ADDR */
static int load_a02os(void)
{
	const uint8_t *sig = (const uint8_t *)(BREC_READ & ~1u);
#ifdef STUB_DEBUG
	for (int i = 0; i < 8; i++)
		DBG->sig[i] = sig[i];
#endif
	for (int i = 0; i < 8; i++)
		if (sig[i] != brec_read_sig[i])
			return 1;                          /* not the BREC we know */
	D(sig_ok, 1);
	brec_read_fn rd = (brec_read_fn)BREC_READ;
	D(lfi_base, LFI_BASE);
	uint32_t base = LFI_BASE >> 9;                /* 0 = primary copy (measured on the device) */
	if (LFI_BASE & 0x1ff)
		return 2;

	uint8_t *dir = (uint8_t *)DIR_BUF;         /* directory: LFI bytes 0x200..0x2000 */
	for (uint32_t s = 0; s < 15; s++)
		rd(base + 1 + s, dir + s * 512, 1, 0);
#ifdef STUB_DEBUG
	for (int i = 0; i < 32; i++)
		DBG->dir0[i] = dir[i];
#endif
	D(stage, 3);
	const uint8_t *e = 0;
	for (uint32_t i = 0; i < 0x1e00; i += 0x20) {
		if (!dir[i])
			break;
		if (names_equal(dir + i, "A02OS   BIN")) {
			e = dir + i;
			break;
		}
	}
	if (!e)
		return 3;
	uint32_t sec = *(const uint32_t *)(e + 0x10);
	uint32_t len = *(const uint32_t *)(e + 0x14);
	uint32_t chk = *(const uint32_t *)(e + 0x1c);
	D(a_sec, sec); D(a_len, len); D(a_chk, chk);
	if (!len || (len & 0x1ff) || len > A02OS_MAX)
		return 4;

	for (uint32_t done = 0; done < len; ) {       /* 32 sectors per call, like BREC itself */
		wd_feed();
		uint32_t n = (len - done) >> 9;
		if (n > 32) n = 32;
		rd(base + sec + (done >> 9), (uint8_t *)A02OS_ADDR + done, n, 0);
		done += n << 9;
	}
	D(stage, 4);
	D(a_sum, sum32((const uint32_t *)A02OS_ADDR, len));
	if (sum32((const uint32_t *)A02OS_ADDR, len) != chk)
		return 5;
	return 0;
}

void STUB_NORETURN stub_main(void)
{
	/* button ladder on (keys.c keys_init) and a cycle counter for timing */
	PAD_CFG17 = 0x303b;
	CMU_DEVCLKEN |= 0x8000000;
	REG(0xc0001000 + 0x7c) = 0;
	PMU(0x38) |= 0x200 | 1;
	DEMCR |= 1u << 24;
	DWT_CYCCNT = 0;
	DWT_CTRL |= 1;
	uint32_t c0 = DWT_CYCCNT;
	for (volatile int i = 0; i < 1000; i++)
		;
	struct timing tm = { cpu_hz() / 1000, DWT_CYCCNT != c0 };
	SYST_CSR = 0;                                  /* free-running, CPU clock, no interrupt */
	SYST_RVR = 0xffffff;
	SYST_CVR = 0;
	SYST_CSR = 5;
#ifdef STUB_DEBUG
	for (unsigned i = 0; i < sizeof(struct dbg) / 4; i++)
		((volatile uint32_t *)DBG)[i] = 0;
	D(magic, 0x57ab0dbe); D(stage, 1); D(hz, tm.per_ms * 1000); D(dwt_ok, tm.dwt_ok);
	D(ladder0, LADDER); D(play0, PLAY_DOWN);
#endif
#ifdef STUB_TEST
	stub_probe(cpu_hz(), LADDER, PLAY_DOWN | (tm.dwt_ok << 8));
#endif

	delay_ms(&tm, 50);                          /* let the ladder ADC settle */
	D(ladder1, LADDER); D(play1, PLAY_DOWN); D(stage, 2);
	/* Play is the power button: it is usually still down here. Wait for its release (the logo
	 * is showing). Still held after 6 s -> USB recovery. Then a ladder button held -> stock.
	 * A misread ladder (e.g. ADC not ready) can only select stock, never recovery. */
	if (held_for(&tm, play_down, 6000)) {
		D(reason, 0x21);
		adfu_reboot();                              /* Play held ~6 s: USB recovery */
	}
	delay_ms(&tm, 30);
	D(ladder1, LADDER); D(play1, PLAY_DOWN);
	if (held_for(&tm, ladder_down, 80)) {           /* M / Vol / Next / Prev / Back held */
		D(reason, 0x10);
#ifdef STUB_DEBUG
		adfu_reboot();
#endif
		stock_boot();
	}
	int why = load_a02os();
#ifdef STUB_TEST
	stub_load_result(why);
#endif
	D(reason, why);
	if (why != 0) {
#ifdef STUB_DEBUG
		adfu_reboot();
#endif
		stock_boot();
	}
	D(stage, 5);
	enter_a02os();
}
