# Installing A02-OS to the player's flash (macOS/Linux, no Windows)

A02-OS can be installed to the player's internal flash so it starts on its own at power-on. The
stock firmware stays installed underneath as a fallback, and the player's USB recovery mode (in
read-only ROM) can always be used to write the stock firmware back.

Verified on an AGPTEK A02 (NAND ID `89 84 64 3c a5`, firmware 1.101.37) on 2026-09-28: stock
rewrite with all 115,196 sectors read back identical, then A02-OS booting from flash. The tools
refuse to run on anything they do not recognize.

> **Risk.** Writing flash is never zero-risk. Everything below verifies before it writes and reads
> back before it reboots, and only the firmware area is written (the boot records are never touched),
> but you use it at your own risk. Charge the player first.

## What happens at power-on

| At power-on | Result |
|---|---|
| Normal power-on (hold Play, let go when the logo shows), or plugging into USB | **A02-OS** |
| Keep holding **M** (or Vol/Next/Prev/Back) through power-on | Stock firmware |
| Keep holding **Play** about 6 s, until the screen goes dark | USB recovery mode (to flash from the computer) |

If anything about A02-OS looks wrong at boot (file missing, checksum bad, unknown boot record), the
player boots the stock firmware instead.

## You need

- macOS (Linux should work) with Python 3, `bsdtar`, `libusb`, and the
  [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
- [actions_flash](https://github.com/ilyakurdyukov/actions_flash) built, with `payload_arm/adfus.bin`
  (see the README). Next to this repo, or `export ACTIONS_DUMP_DIR=path`.
- Rockbox's `atjboottool`: `git clone https://github.com/Rockbox/rockbox` next to this repo, then
  `make -C rockbox/utils/atj2137/atjboottool` (or set `ATJBOOTTOOL` / `ATJBOOTTOOL_SRC`).

## Install

1. **Fetch the official firmware** (downloaded from AGPTEK, SHA-256 checked before anything is
   unpacked, stored in `~/Library/Caches/a02-os/vendor`, never in this repo):
   `python3 tools/flash/fetch_firmware.py`
2. **Build the image:** `cd os && make image` → `os/a02os-boot.lfi` (exactly the stock size; see below).
3. **Recovery mode:** from the stock firmware in USB-disk mode, `diskutil eject <volume>` then, in the
   actions_flash folder, `sudo ./actions_dump --wait 10 --id 10d6:1101 chip 2157 adfu_reboot`. Once
   A02-OS is installed: hold Play ~6 s at power-on instead. **Start from a power-on**, not from a
   software reboot right after another USB session (the flash driver needs a clean start).
4. *(Optional, read-only)* rehearse: `python3 tools/flash/adfu_dryrun.py` — runs the whole vendor
   sequence with a flash driver whose erase/program are disabled (proven by `tools/unbrick/nand_gate.py`),
   reads the firmware back and compares it with the rebuild. Nothing can be written.
5. **Flash:** `python3 tools/flash/adfu_flash.py --write --image os/a02os-boot.lfi`.
   Ends with `VERIFIED: all 115196 sectors read back identical.` Then slide the switch off, and power on.

**Update A02-OS:** rebuild (`make image`), recovery mode (hold Play ~6 s), step 5 again.
**Back to stock:** recovery mode, then `python3 tools/flash/adfu_flash.py --write` (no `--image`
writes the official firmware, rebuilt byte-for-byte).

### Alternative: the stock firmware's own updater (no USB tools)

The stock firmware can reflash itself from a file: **Tools → Firmware upgrade** reads `UPGRADE.HEX` from
the root of its disk. `python3 tools/flash/mkupgrade.py stock -o UPGRADE.HEX` builds the official
firmware in that format; `tools/flash/copy-to-player.sh` copies it, reads it back and ejects.
(`mkupgrade.py verify` replays every check the player makes.)

## How it works

**The Windows tool is a script interpreter.** The official "Audio Products Volume-Production tools"
run a script stored inside the firmware file itself (`FuncSpec`, function `AdfuUpgrade`). Its USB
commands are the ADFU commands `actions_dump` already speaks. `tools/flash/adfu_prep.py` computes
what that script computes:

1. ADFUS loader → 0x118000, switch.
2. `nandhwsc.bin` → 0x11E000, call, read the 0x9C-byte scan result (`HW`, flash ID at +16, chip-record
   slot at +0x18 = 0x125400).
3. The 128-byte record for this flash ID from `FLASH_ID.BIN` → the slot. *(Skipping this makes the
   flash driver hang — the reason earlier attempts failed.)*
4. Flash driver `fwscf651.bin` (picked by flash ID) with its header patched (+2 erase flag 0, +6/+8
   sizes, +128 pin config, +253..255 flags) → 0x11E000, call +0x200, read status (`NAFWSC`).
5. Storage writes: `0xC0…` firmware image (LFI), `0xFF…` finalize; reads `0x80…`.

**Boot hook.** Boot ROM → MBREC (0x101000) → BREC (0x10E900) checks both firmware copies, loads
`KER_TEXT`, `KER_DATA`, `KER_INIT` and calls `KER_INIT` at 0x108001. `os/boot/stub.c` *is* that
`KER_INIT`: it checks the buttons, verifies BREC's NAND-read function by signature, reads the
firmware directory, loads `A02OS.BIN` to 0x120000, checks its checksum and starts it. The stock path
copies the original `KER_INIT` (carried inside the stub) back into place and continues exactly as
if the stub had never run. `make run-stubtest` runs the same decision code from RAM and reports.

**Exact size.** BREC rejects a firmware image whose total size differs from the value in its own
header (`lfi_size not match!`). `tools/flash/mk_a02os_lfi.py` keeps the image exactly the stock size
by trimming language tables English never uses (Thai first), in place.

Measured on the device: the ARM cycle counter does not run (the stub times with SysTick), the
boot ROM leaves the watchdog running (the stub feeds it), the primary firmware copy starts at offset 0.

## Legal

This repository contains no vendor firmware or code. The tools download the official update from
the vendor, verify it by hash, and patch/repackage it on your own machine for your own device.
`tools/flash/fwkey.c` includes Rockbox code and is GPL-2.0-or-later; everything else is MIT.
