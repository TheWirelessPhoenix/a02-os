# AGPTEK A02 hardware notes (Actions ATJ2157)

Everything here was found by reverse engineering the player (observing its behaviour, reading register
usage of the stock firmware in Ghidra, and testing on the device). No vendor code is included in this repo.

## SoC

- **Actions ATJ2157**, ARM Cortex-M4F, 224 KB SRAM at `0x100000–0x137FFF`, mask-ROM with USB recovery ("ADFU").
- USB IDs: player mode `10d6:1101`, recovery/ADFU mode `10d6:10d6`.
- Stock firmware: Actions "US215A" SDK (uC/OS-II based), `.fw` update = encrypted SQLite container
  (decryptable with Rockbox's `atjboottool`).

## Pins (GPIO numbers)

| Function | Pin(s) |
|---|---|
| LCD D0–D7 | GPIO0–7 (8080 8-bit parallel) |
| LCD RS / RD / WR / CS | 8 / 10 / 11 / 43 |
| LCD RESET | 21 |
| Backlight | 22 (PWM) |
| Keys | LRADC on GPIO17 (resistor ladder) |
| microSD (4-bit) | CLK 30, CMD 32, D0 33, D1 29, D2 28, D3 27, detect 19 |
| FM I2C | SCL 24, SDA 25 |
| Second I2C bus | 55 / 56 |
| SWD | GPIO62/63 |

## Clocks and reset

| Register | Use |
|---|---|
| `0xC0000000` | reset/enable: bit2 SD, bit13 LCD, bit25 PWM |
| `0xC0000004` | bit2 audio |
| `0xC0000110` / `0xC0000114` | COREPLL / DEVPLL: bits[6:0] × 6 MHz, bit7 enable |
| `0xC0000118` / `0xC000011C` | audio PLLs |
| `0xC0001000` (CMU) | bits[1:0] CPU source: 1 = 24 MHz crystal, 2 = DEVPLL, 3 = COREPLL; bits[5:4] divider |
| CMU `+0x08` | device clock enables (bit0, bit2 SD, bit13 LCD, bit25 PWM, bit27 LRADC) |
| CMU `+0x0C` | audio clock enables (bit2, bit11) |
| CMU `+0x14` | SD clock (0x41 = slow identify, then divider 10) |
| CMU `+0x34` | LCD clock divider |
| CMU `+0x88` | DAC sample-rate divider: `(x & 0xFFFFFFA8) | fam44<<6 | half<<4 | idx`, dividers {1,2,3,4,6,8,12} of 192 kHz (48k family) or 176.4 kHz (44.1k family) |

Recovery mode leaves the CPU on DEVPLL at 48 MHz. A02-OS runs the CPU from COREPLL at 96 MHz.

## Watchdog / RTC

- RTC block `0xC0030000`; watchdog control `+0x1C` (bit0 = feed).

## LCD

- Controller `0xC01A0000`: `+0x00` control, `+0x14` data/command FIFO.
  - command: `CTL = (CTL & 0xE7FFFF3F) | 1; FIFO = cmd`
  - 8-bit data: `CTL = (CTL & 0xE7EFFF3F) | 0x100040; FIFO = byte`
  - RGB565 pixels: `CTL = (CTL & 0xE7CFFF37) | 0x40; FIFO = r5<<19 | g6<<10 | b5<<3`
  - bit28 of CTL acts as a bus lock (cleared during access).
- Pixel writes are done with the CPU on the 24 MHz crystal (as the stock driver does), and each FIFO write is
  paced with a read-back — an unpaced burst drops pixels.
- Panel: 128×160, ST7735S-compatible command set. Init sequence is in `os/src/lcd.c`.
- Backlight PWM `0xC0190400`: `+0x00` control, `+0x14` period, `+0x18` duty.

## Keys

- Ladder ADC: PMU `0xC0010064` (10-bit, idle ≈ 837). Levels: Next ≈ 0, Prev ≈ 140, M ≈ 279, Down ≈ 420, Back ≈ 559.
- Play/Pause (also power): PMU `0xC001002C` bit1.
- Battery ADC: PMU `0xC001003C`.
- Volume rocker: not found yet.

## microSD

- Controller `0xC0160000`: `+0x04` command control (bit7 go/busy), `+0x08` status (errors `0x8017`), `+0x0C`
  command, `+0x10` argument, `+0x14..0x20` response, `+0x28` data FIFO, `+0x2C` block size, `+0x30` block count.
- Data via DMA channel 4 (`0xC0070500`), config `0x85` for reads.
- Standard SD init (CMD0/8/ACMD41/2/3/9/7/16/ACMD6), SDHC supported, 4-bit bus.

## Audio out

- Power-up: PMU `+0x18` = `(x & ~0x20) | 0x40`, audio reset release, audio PLLs.
- DAC `0xC0180000`: `+0x00` control, `+0x04` FIFO control, `+0x08` status (bit7 FIFO ready), `+0x0C` FIFO,
  `+0x14/+0x18` digital volume L/R `(x & 0xFFF00F00) | 0x33700 | vol`, `+0x28` PA control, `+0x2C` PA stages.
- The A02 uses AC-coupled headphone output; the PA is powered up with a slow 30 000-sample ramp to avoid pops.
- Streaming: DMA channel 2 (`0xC0070300`) in reload mode, config `0x148B00`, destination DAC FIFO.
  Samples are 16-bit stereo, one frame per 32-bit word. The transfer-complete flag is `0xC0070000` bit2
  (write 1 to clear). In reload mode the channel restarts from its registers, so the next buffer must be
  programmed *before* the current one ends (queue-ahead with 3 buffers).

## Recording (not implemented yet)

- ADC block `0xC0181000`. Stock defaults: mic analog gain 14–33 dB, digital gain 0–59 × 0.526 dB.
