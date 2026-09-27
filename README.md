# A02-OS

An open-source replacement OS for the **AGPTEK A02** MP3 player (Actions ATJ2157, ARM Cortex-M4F).

Fast, simple, themeable, and it sounds great.

> **Status: v0.1 preview.** A02-OS runs from RAM and is loaded into the player over USB from a computer.
> It is not installed to the player's flash yet, so the stock firmware is untouched and a power cycle
> always brings it back. Installing to flash is planned once a tested recovery tool exists.

## What works

- Plays **MP3** (via [minimp3](https://github.com/lieff/minimp3)) and **WAV** from a microSD card
  (FAT32/exFAT via [FatFs](http://elm-chan.org/fsw/ff/)), native 44.1/48 kHz, gapless DMA streaming
- Home menu, folder browser, Now Playing with live level visualizer, ID3 title/artist, progress and time
- Pause, next/previous track, volume
- 5 themes: Paper (default), Midnight, Neon, Terminal, Sunset — pixel font
- Settings: backlight, volume

## Not yet

- Lecture recorder (microphone input), volume rocker, FLAC/AAC, saving settings, internal storage,
  installing to flash, USB mass storage

## Controls

| Button | Lists | Now Playing |
|---|---|---|
| M (up) / Down | move | volume up / down |
| ▶▶ or ▶❚❚ | open / select | ▶▶ next track · ▶❚❚ pause |
| ◀◀ or Back | back | ◀◀ previous · Back to list |

Press **Back three times** on the home screen to exit to the USB loader.

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

To return to the stock firmware: slide the power switch off, unplug, plug back in, switch on.

## Layout

```
os/src/        A02-OS (drivers: lcd, keys, sd, audio; player; UI)
os/lib/        FatFs, minimp3 (third-party, see their licenses)
docs/          HARDWARE.md — everything learned about the hardware
tools/         yqhx2elf.py (stock driver -> ELF for Ghidra), annotate.py, Ghidra export script
```

## How this was made

The hardware was mapped by reverse engineering: decrypting the stock firmware with Rockbox's
`atjboottool`, loading its drivers into Ghidra (`tools/yqhx2elf.py`), and testing every finding on the device
from RAM with `actions_flash`. See [docs/HARDWARE.md](docs/HARDWARE.md).

## Disclaimer

Not affiliated with AGPTEK or Actions Semiconductor. Use at your own risk. This repository contains no vendor
firmware or code.

## License

MIT — see [LICENSE](LICENSE). Third-party libraries keep their own licenses
(FatFs: BSD-style, minimp3: CC0).
