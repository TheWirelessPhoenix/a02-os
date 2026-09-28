# A02-OS

An open-source replacement OS for the **AGPTEK A02** MP3 player (Actions ATJ2157, ARM Cortex-M4F).

Fast, simple, themeable, and it sounds great.

> **Status: v0.2.1 preview.** A02-OS runs from RAM and is loaded into the player over USB from a computer.
> It is not installed to the player's flash yet, so the stock firmware is untouched and a power cycle
> always brings it back. Installing to flash is planned once a tested recovery tool exists.

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

- Side volume rocker, FLAC/AAC, sleep timer, equalizer, internal storage, installing to flash,
  USB mass storage

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
switch on.

Host test of the speaker/headphone privacy logic: `cd os && make route-test`.

## Layout

```
os/src/        A02-OS (drivers: lcd, keys, sd, audio, adc; player; recorder; speaker/jack guard; power; UI)
os/host/       route_test.c (host test), reload.sh (reboot the player into recovery)
os/lib/        FatFs, minimp3 (third-party, see their licenses)
docs/          MANUAL.md — how to use it; HARDWARE.md — everything learned about the hardware
tools/         yqhx2elf.py (stock driver -> ELF), ap2elf.py (stock app -> ELF), annotate.py,
               ghidra/ export scripts (DumpDecomp, BankFuncs)
```

## How this was made

The hardware was mapped by reverse engineering: decrypting the stock firmware with Rockbox's
`atjboottool`, loading its drivers and apps into Ghidra (`tools/yqhx2elf.py`, `tools/ap2elf.py`), and testing every finding on the device
from RAM with `actions_flash`. See [docs/HARDWARE.md](docs/HARDWARE.md).

## Disclaimer

Not affiliated with AGPTEK or Actions Semiconductor. Use at your own risk. This repository contains no vendor
firmware or code.

## License

MIT — see [LICENSE](LICENSE). Third-party libraries keep their own licenses
(FatFs: BSD-style, minimp3: CC0).
