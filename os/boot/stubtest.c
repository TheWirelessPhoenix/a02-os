/* RAM test for the boot stub's decision logic: runs the real stub_main() (built with
 * -DSTUB_TEST) from USB recovery and reports what it saw and which path it chose.
 * Result block at 0x102000 (like the other payloads): magic, hz, ladder, play, load, path,
 * then ladder samples over ~1 s. */
#include "stubtest.h"

#define REG(a) (*(volatile uint32_t *)(a))
struct result { uint32_t magic, hz, ladder, play, load, path, samples[16]; };
static struct result *const R = (struct result *)0x102000;

void stub_main(void);
void stub_result(int path) { R->path = (uint32_t)path; }
void stub_probe(uint32_t hz, uint32_t ladder, uint32_t play) { R->hz = hz; R->ladder = ladder; R->play = play; }
void stub_load_result(int why) { R->load = (uint32_t)why; }

int main(void)
{
	for (unsigned i = 0; i < sizeof *R / 4; i++)
		((volatile uint32_t *)R)[i] = 0;
	R->load = 0xff;
	stub_main();
	uint32_t per_ms = R->hz / 1000;
	for (int i = 0; i < 16; i++) {               /* ladder over ~1.6 s: should sit at idle */
		R->samples[i] = REG(0xc0010064) & 0x3ff;
		uint32_t t0 = REG(0xe0001004);
		(void)t0;
		uint32_t last = REG(0xe000e018), acc = 0;   /* SysTick, like the stub */
		for (uint32_t n = 0; n < 100 * (per_ms / 4) && acc < 100 * per_ms; n++) {
			uint32_t now = REG(0xe000e018);
			acc += (last - now) & 0xffffff;
			last = now;
			REG(0xc003001c) |= 1;               /* feed the watchdog; capped like the stub */
		}
	}
	R->magic = 0x57b0057b;
	return 0;
}
