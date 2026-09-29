#!/bin/bash
# Copy a verified UPGRADE.HEX onto the A02's internal disk, read it back, clean macOS
# metadata and eject. It does NOT start the update: that happens on the player
# (Tools > Firmware upgrade).
#   usage: copy-to-player.sh [UPGRADE.HEX] [volume, default /Volumes/AGP-A02]
set -euo pipefail
SRC="${1:-$HOME/Downloads/A02-flash/UPGRADE.HEX}"
VOL="${2:-/Volumes/AGP-A02}"
HERE="$(cd "$(dirname "$0")" && pwd)"

[ -f "$SRC" ] || { echo "no such file: $SRC"; exit 1; }
[ -d "$VOL" ] || { echo "player not mounted at $VOL (plug it in, normal mode, and wait for the disk)"; exit 1; }

echo "1/5 verifying $SRC (same checks the player's updater does)..."
python3 "$HERE/mkupgrade.py" verify "$SRC" | tail -1

need=$(stat -f %z "$SRC")
free=$(df -k "$VOL" | awk 'NR==2 {print $4 * 1024}')
[ "$free" -gt $((need + 16*1024*1024)) ] || { echo "not enough free space on $VOL"; exit 1; }

echo "2/5 copying to $VOL/UPGRADE.HEX ..."
rm -f "$VOL/UPGRADE.HEX" "$VOL/._UPGRADE.HEX"
cp -X "$SRC" "$VOL/UPGRADE.HEX"
sync

echo "3/5 reading it back from the player (remount first so nothing comes from the Mac's cache) ..."
DEV=$(diskutil info "$VOL" | awk -F': *' '/Device Identifier/ {print $2}')
diskutil unmount "$VOL" >/dev/null
diskutil mount "$DEV" >/dev/null
[ -f "$VOL/UPGRADE.HEX" ] || { echo "remount failed or file missing at $VOL"; exit 1; }
want=$(shasum -a 256 "$SRC" | cut -d' ' -f1)
got=$(shasum -a 256 "$VOL/UPGRADE.HEX" | cut -d' ' -f1)
if [ "$want" != "$got" ]; then
    rm -f "$VOL/UPGRADE.HEX"
    echo "MISMATCH after copy — removed it. Do not upgrade; try again."
    exit 1
fi
echo "    sha256 $got  (matches)"

echo "4/5 removing macOS metadata files ..."
dot_clean -m "$VOL" 2>/dev/null || true
rm -f "$VOL/._UPGRADE.HEX"

echo "5/5 ejecting ..."
diskutil eject "$VOL"
echo "DONE. Unplug, then on the player: Tools > Firmware upgrade."
