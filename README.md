# A02-OS

An open-source replacement OS for the **AGPTEK A02** MP3 player (Actions ATJ2157, ARM Cortex-M4F).

Fast, simple, themeable, and it sounds great.

> **Status: v0.2.1 + flash install.** A02-OS can now be **installed to the player's flash** from a Mac
> (no Windows) and starts at power-on; the stock firmware stays underneath as a fallback (hold M at
> power-on) and can be written back at any time. See [docs/FLASH-INSTALL.md](docs/FLASH-INSTALL.md).
> It still runs from RAM over USB too, for development.

## What works

- Plays **MP3** (via [minimp3](https://github.com/lieff/minimp3)) and **WAV** from a microSD card
  (FAT32/exFAT via [FatFs](http://elm-chan.org/fsw/ff/)), native 44.1/48 kHz, gapless DMA streaming
- Home menu, folder browser, Now Playing with live level visualizer, ID3 title/artist, progress and time
- Pause, next/previous track, volume with an on-screen volume bar. Pause really stops the sound.
- **Lecture recorder**: records the built-in mic to `/REC/LECT####.WAV` on the card (mono 24 kHz) with
  pause/resume and bookmarks, then plays it back
- **Built-in speaker with a privacy lock**: the speaker only plays after you press Play with no headphones in;
  unplugging headphones never switches sound to the speaker. **JACK GUARD** (Settings, default ON) pauses
  when headphones are plugged in or out.
- **Power**: hold Back 2 s for POWER OFF / RESTART / RELOAD VIA USB. Hold Back 6 s or Play 10 s to recover
  from a freeze.
- 5 themes: Paper (default), Midnight, Neon, Terminal, Sunset — pixel font
- Settings: backlight, volume, JACK GUARD, battery; **saved to the card** (`A02OS.CFG`) so they survive restarts
- Battery level in the header

## Not yet

- Side volume rocker, FLAC/AAC, sleep timer, equalizer, internal storage, USB mass storage
  (copy music with the stock firmware for now: hold M at power-on)

## Controls

| Button | Lists | Now Playing |
|---|---|---|
| M (up) / Down | move | volume up / down |
| ▶▶ or ▶❚❚ | open / select | ▶▶ next track · ▶❚❚ pause |
| ◀◀ or Back | back | ◀◀ previous · Back to list |
| Hold Back 2 s | POWER screen | POWER screen |

The full guide (recorder, speaker, POWER screen, troubleshooting) is in **[docs/MANUAL.md](docs/MANUAL.md)**.

## Running it

You need: an A02, a microSD card with music (FAT32/exFAT), macOS or Linux, and
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi-gcc`).

1. Build [actions_flash](https://github.com/ilyakurdyukov/actions_flash) (libusb) and its ARM `adfus.bin` payload
   (`payload_arm/`, `make NAME=adfus`). Put the checkout next to this repo, or pass `ACTIONS_FLASH=path`.
2. Build A02-OS: `cd os && make`
3. Put the player in recovery (ADFU) mode. From normal USB-disk mode:
   ```
   diskutil eject <player volume>        # macOS; unmount on Linux
   sudo ./actions_dump --wait 10 --id 10d6:1101 chip 2157 adfu_reboot
   ```
   It now enumerates as `10d6:10d6`.
4. Run: `cd os && make run`

To load a new build, you don't need to power cycle. On the player choose POWER (hold Back 2 s) →
**RELOAD VIA USB**, or press Back three times on Home, then `make run` again. If A02-OS has already exited,
`make reload` reboots the player into recovery from the computer (macOS).

To return to the stock firmware: POWER → **RESTART (STOCK)**, or slide the power switch off, unplug, plug back in,
switch on. Once A02-OS is installed to flash, hold **M** while powering on to start stock instead.

**Install to flash:** [docs/FLASH-INSTALL.md](docs/FLASH-INSTALL.md) (`python3 tools/flash/fetch_firmware.py`,
`cd os && make image`, then `python3 tools/flash/adfu_flash.py --write --image os/a02os-boot.lfi`).

Host test of the speaker/headphone privacy logic: `cd os && make route-test`.

## Layout

```
os/src/        A02-OS (drivers: lcd, keys, sd, audio, adc; player; recorder; speaker/jack guard; power; UI)
os/boot/       boot stub that starts A02-OS from flash (replaces KER_INIT.BIN) + its RAM test
os/host/       route_test.c (host test), reload.sh (reboot the player into recovery)
os/lib/        FatFs, minimp3 (third-party, see their licenses)
docs/          MANUAL.md — how to use it; HARDWARE.md — the hardware; FLASH-INSTALL.md — install to flash
tools/flash/   Mac/Linux flasher (the vendor ADFU procedure), image builder, UPGRADE.HEX builder, fetcher
tools/unbrick/ official-firmware downloader + read-only flash-driver patch and its proof (nand_gate.py)
tools/         yqhx2elf.py (stock driver -> ELF), ap2elf.py (stock app -> ELF), annotate.py,
               ghidra/ export scripts (DumpDecomp, BankFuncs)
```

## How this was made

The hardware was mapped by reverse engineering: decrypting the stock firmware with Rockbox's
`atjboottool`, loading its drivers and apps into Ghidra (`tools/yqhx2elf.py`, `tools/ap2elf.py`), and testing every finding on the device
from RAM with `actions_flash`. See [docs/HARDWARE.md](docs/HARDWARE.md).

## Disclaimer

Not affiliated with AGPTEK or Actions Semiconductor. Use at your own risk. This repository contains no vendor
firmware or code: the flash tools download the official update from AGPTEK and verify it by hash on your machine.
`tools/flash/fwkey.c` includes Rockbox code (GPL-2.0-or-later).

## License

MIT — see [LICENSE](LICENSE). Third-party libraries keep their own licenses
(FatFs: BSD-style, minimp3: CC0).
