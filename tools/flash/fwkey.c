/* SPDX-License-Identifier: GPL-2.0-or-later (it includes Rockbox code)
 * fwkey: print the FWU v3 session key of an Actions .fw/UPGRADE.HEX file.
 * Built against Rockbox's atjboottool (GPL-2.0+); includes fwu.c to reach its static helpers.
 * Output (stdout, one line): "<keybuf 32 bytes hex> <blockA> <blockB>" */
#include "fwu.c"   /* from Rockbox utils/atj2137/atjboottool, via -I (see Makefile) */

int main(int argc, char **argv)
{
    if(argc != 2) { fprintf(stderr, "usage: fwkey file.fw\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if(!f) { perror("fopen"); return 2; }
    uint8_t *buf = malloc(0x4800);
    if(fread(buf, 1, 0x4800, f) != 0x4800) { fprintf(stderr, "short file\n"); return 2; }
    fclose(f);
    enable_color(false);
    FILE *so = stdout; stdout = stderr;  /* library chatter to stderr */
    uint8_t blockA, blockB, keybuf[32], blo[512];
    memset(keybuf, 0, sizeof(keybuf));
    int ret = get_key_fwu_v3(0x4800, buf, &blockA, &blockB, keybuf, blo);
    stdout = so;
    if(ret) { fprintf(stderr, "key derivation failed (%d)\n", ret); return 1; }
    for(int i = 0; i < 32; i++) printf("%02x", keybuf[i]);
    printf(" %d %d\n", blockA, blockB);
    return 0;
}
