# Installing A02-OS to flash — status and plan

A02-OS currently runs **from RAM**: it is loaded into the player over USB from a computer and the
player's flash is never touched, so a power cycle always restores the stock firmware. This document
tracks the work to (a) install A02-OS to the player's flash and (b) provide a **tested recovery path**
so an update can always be undone.

Nothing here requires trusting us with your device's boot records: the plan is backup-first, and
restores target the firmware area, not the boot/recovery records.

## Why this is hard

The A02 (Actions ATJ2157) keeps its firmware in an **LFI** container on a NAND flash chip behind a
controller with an FTL (flash translation layer). Writing flash means speaking the controller's
command protocol and the FTL — the vendor driver does this, and its power-on path can erase and
program flash. So the two goals are one: **a verified backup plus a proven, reversible write.**

## Where we are

| Step | State |
|---|---|
| Read the NAND over USB (read-only) | ✅ works |
| Boot records (MBREC/BREC) backed up, read twice, byte-identical | ✅ |
| Is the firmware scrambled? | ✅ **No** — main data is stored in the clear (verified: boot record and firmware headers match byte-for-byte) |
| Locate the firmware (LFI) on NAND | ✅ found; header and directory parse (`KERNEL.DRV`, `KER_TEXT.BIN`, …) |
| Understand the FTL | 🟡 it is a standard **block-mapped** FTL (logical block → physical block, 16-bit table) |
| Read the whole firmware natively | 🟡 needs the block map decoded (in progress) |
| Write / restore natively | ❌ not yet |
| Flash-install + auto-boot | ❌ not started |

## Plan

1. **Recovery first.** Confirm a factory restore works on a spare/known unit using the vendor's own
   updater before any custom write. This converts "brick risk" into "recoverable."
2. **Native read.** Decode the FTL block map and reassemble the firmware from a raw read; verify it
   against the official update contents.
3. **Native write.** Implement a controlled write path (verify-before-reboot) so a restore — and later
   the A02-OS install — can be driven from any OS.
4. **Install design.** Decide how A02-OS presents itself at boot and package it as a repeatable update.

## Guarantees we keep

- Never touch the boot/recovery records unless it is the explicit last resort.
- Verify every written region by reading it back before rebooting.
- Keep the stock firmware and a verified backup available for rollback.

*This file is documentation only; no vendor code or firmware is distributed here.*
