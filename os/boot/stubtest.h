/* Hooks for the RAM test build of the boot stub (make run-stubtest). */
#include <stdint.h>
void stub_result(int path);                           /* 1 stock, 2 A02-OS, 3 recovery */
void stub_probe(uint32_t hz, uint32_t ladder, uint32_t play);
void stub_load_result(int why);                        /* load_a02os() return code */
