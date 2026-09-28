/* `make reload`: reboot into USB recovery (ADFU) from the loader, so the next `make run`
 * needs no power cycle. Works from the boot ROM ADFU or from adfus after a payload returned. */
#include "power.h"

void *main(void)
{
	power_reboot_adfu();
}
