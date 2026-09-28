# A02-OS user manual (v0.2)

A02-OS runs on the **AGPTEK A02** from RAM. You load it from a computer over USB (see
[Getting started](#getting-started)). Your music and recordings live on the microSD card.

## Buttons

| Button | Where it is |
|---|---|
| **▶❚❚ Play/Pause** | center of the pad (also the power button) |
| **▶▶ Next** / **◀◀ Prev** | right / left of the pad |
| **M** / **Down** | top / bottom of the pad |
| **Back** | the separate Back key |

The volume rocker on the side is not supported yet. Use M / Down on the Now Playing screen.

### Everywhere

| Do this | Result |
|---|---|
| Press **Back** | go back one screen (it acts when you let go) |
| **Hold Back 2 s** | open the **POWER** screen |
| **Hold Back 6 s** | emergency restart into USB recovery. Works even if the player is frozen. |
| **Hold Play/Pause 10 s** | emergency restart into USB recovery. Works even if the player is frozen. |

### Menus and lists

| Button | Action |
|---|---|
| M / Down | move up / down |
| ▶▶ or ▶❚❚ | open / select |
| ◀◀ or Back | back |
| Back ×3 on the Home screen | restart into USB recovery (for developers) |

### Now Playing

| Button | Action |
|---|---|
| ▶❚❚ | pause / resume |
| ▶▶ / ◀◀ | next / previous track |
| M / Down | volume up / down |
| Back | back to the list (stops playback) |

### Recorder

| Screen | ▶❚❚ or ▶▶ | M | Back or ◀◀ |
|---|---|---|---|
| Ready | start recording | — | leave |
| Recording | pause | add a bookmark | stop and save |
| Paused | resume | stop and save | stop and save |
| Saved | play the recording | — | leave |

Recordings are saved to the card as `/REC/LECT0001.WAV`, `LECT0002.WAV`, and so on (mono, 24 kHz). Bookmarks
go in a matching `.MRK` file.

## POWER screen (hold Back 2 s)

| Option | What it does |
|---|---|
| **POWER OFF** | Goodbye animation, then the player turns off. While it is plugged into USB it cannot turn off (USB keeps it powered). It shows **UNPLUG USB CABLE** and turns off when you unplug. Press ▶❚❚ there to restart into USB recovery instead. |
| **RESTART (STOCK)** | Restarts into the original AGPTEK firmware. |
| **RELOAD VIA USB** | Restarts into USB recovery, so the computer can load A02-OS again (`make run`). |
| **CANCEL** | Back to where you were. Music stays paused; press ▶❚❚ to resume. |

M / Down moves, ▶❚❚ or ▶▶ selects, Back cancels. If a recording is running, any action saves it first. Opening the
menu does not stop the recording.

Turning the player on again starts the original firmware for now, because A02-OS is not installed to flash yet.

## Speaker and headphones

The built-in speaker is locked down for privacy:

- The speaker plays only after **you press ▶❚❚** (or pick a track) with no headphones plugged in.
- **Unplugging headphones never switches sound to the speaker.** The speaker stays off until you press ▶❚❚ again.
- **Plugging headphones in** turns the speaker off on the spot.
- If the headphone jack reading is unclear (for example, a half-inserted plug), the player stays silent.
- While music plays on the speaker, holding M or Down (volume) mutes the speaker for as long as you hold it.
  Those buttons share a sensing line with the jack. Headphone listening is not affected.

### JACK GUARD (Settings)

| Setting | Behavior |
|---|---|
| **ON** (default) | Plugging in *or* unplugging headphones pauses playback right away. It never resumes by itself. |
| **OFF** | Plugging and unplugging never pause. After unplugging, the music keeps going silently, because the speaker still stays off. Press ▶❚❚ twice (pause, then play) to hear it on the speaker. |

Settings are not saved yet. JACK GUARD is back ON after every restart.

## Settings

| Row | Press ▶▶ / ▶❚❚ to |
|---|---|
| BACKLIGHT | cycle brightness |
| VOLUME | step the volume |
| JACK GUARD | switch ON / OFF |
| EQUALIZER, SLEEP TIMER | not available yet |
| ABOUT A02-OS | shows the version |

## Themes

Home → THEMES, then pick Paper (default), Midnight, Neon, Terminal or Sunset.

## Supported files

- **MP3** and **WAV** (16-bit, mono or stereo) from a FAT32 or exFAT microSD card
- Put music anywhere. A `/Music` folder opens first if it exists.

## Getting started

You need a computer with macOS (Linux should work but is untested) and the tools listed in the
[README](../README.md#running-it).

1. Put the player in USB recovery mode. From the stock firmware in USB-disk mode:
   `sudo ./actions_dump --wait 10 --id 10d6:1101 chip 2157 adfu_reboot` (in the actions_flash folder).
   It then shows up as `10d6:10d6`.
2. `cd os && make run`. A02-OS appears on the player.
3. **To reload after a change:** on the player, use POWER → **RELOAD VIA USB** (or triple-Back on Home), then
   `make run` again. No power cycle or `sudo` needed. If A02-OS has already exited, `make reload` does the same
   from the computer.

## Troubleshooting

| Problem | Fix |
|---|---|
| Player frozen | Hold **Back 6 s** or **Play/Pause 10 s**. It restarts into USB recovery. Then `make run`. |
| "INSERT SD CARD" | Insert a FAT32/exFAT card and run again. |
| No sound from the speaker | Unplug headphones and press ▶❚❚. The speaker only starts on your press. |
| Music paused when I plugged or unplugged headphones | That is JACK GUARD. Press ▶❚❚ to continue, or turn it OFF in Settings. |
| Want the original firmware back | POWER → RESTART (STOCK). Or slide the power switch off, unplug, plug back in, switch on. |
