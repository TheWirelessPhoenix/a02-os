# fwkey links Rockbox's atjboottool sources (GPL-2.0-or-later). Point R at them if they are elsewhere.
R ?= $(firstword $(wildcard $(ATJBOOTTOOL_SRC) ../rockbox/utils/atj2137/atjboottool ../../../rockbox/utils/atj2137/atjboottool))
fwkey: fwkey.c
	@test -n "$(R)" || { echo "set ATJBOOTTOOL_SRC to rockbox/utils/atj2137/atjboottool"; exit 1; }
	$(CC) -O2 -std=gnu99 -w -I$(R) -o $@ fwkey.c $(R)/misc.c $(R)/atj_tables.c $(R)/afi.c -lz
