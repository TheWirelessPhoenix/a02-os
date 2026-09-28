#!/bin/sh
# Reboot the A02 into USB recovery (boot-ROM ADFU, 10d6:10d6) without a power cycle or sudo.
# Works while a loader is listening: the boot ROM, or adfus after a payload returned.
# Env: ACTIONS_FLASH = built actions_flash checkout, BIN = reboot.bin (set by `make reload`).
# macOS only for now (uses ioreg to watch the USB re-enumeration).
cd "$ACTIONS_FLASH" || exit 1
sid() { ioreg -p IOUSB -l 2>/dev/null | grep -A25 -B25 '"idVendor" = 4310' | grep '"sessionID"' | tr -dc '0-9'; }
wait_new() { # $1 = old session id; waits up to $2 half-seconds for a different one
	i=0
	while [ $i -lt "$2" ]; do
		s=$(sid)
		[ -n "$s" ] && [ "$s" != "$1" ] && return 0
		sleep 0.5
		i=$((i + 1))
	done
	return 1
}
b=$(sid)
[ -n "$b" ] || { echo "reload: no A02 in USB recovery mode (10d6:10d6) found"; exit 1; }
# 1) adfus already running (a payload just returned): exec the reboot stub directly
./actions_dump --wait 5 chip 2157 timeout 3000 blk_size 0x4000 simple_exec 0x120001 "$BIN" 0 >/dev/null 2>&1
if ! wait_new "$b" 6; then
	# 2) plain boot ROM: load adfus first
	./actions_dump --wait 5 chip 2157 timeout 5000 simple_switch 0x118000 payload_arm/adfus.bin \
		simple_exec 0x120001 "$BIN" 0 >/dev/null 2>&1
	wait_new "$b" 12 || { echo "reload: device did not re-enumerate"; exit 1; }
fi
sleep 1
./actions_dump --wait 10 chip 2157 timeout 3000 adfu_info 2>&1 | grep -aq CADFU \
	&& echo "reload: device is back in USB recovery mode, ready for make run" \
	|| { echo "reload: re-enumerated but no ADFU answer"; exit 1; }
