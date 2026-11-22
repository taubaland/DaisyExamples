# QspiWriter

A flash loader for the Boonta's QSPI chip. It is the only working way to put
an image at `0x90040000` on this board.

```
./qspi-write.sh <file> [address]        # address defaults to 0x90040000
make write FILE=<file> [ADDR=<addr>]    # same thing, building first
```

It is also what `../NewEffectDuino/flash.sh BOOT_SRAM` and `BOOT_QSPI` call.

## Why

An image reaches QSPI over DFU, and DFU does not work on this board: the Daisy
bootloader never enumerates, because libDaisy's USB stack does not come up here
-- the same fault that sinks libDaisy's own `seed/USB_MIDI` example. The chip's
own ROM bootloader does enumerate, but it offers only `@Internal Flash` and
`@Option Bytes`, so it cannot help. openocd's `stmqspi` driver drops the SWD
link the moment it probes, with QUADSPI demonstrably configured; it is not
usable over ST-Link's HLA transport. `../NewEffectDuino/PORTING.md` has the
evidence for all three.

So this takes the route every real flash loader takes: a program that runs on
the target and does the writing, driven by a debugger that knows nothing about
the flash. libDaisy's `QSPIHandle` already knows the IS25LP064A, which leaves
little to write but a mailbox and a loop.

## How it works

Three pieces:

| | |
|---|---|
| `QspiWriter.cpp` | the target-side program. Erases, writes, verifies. |
| `QspiWriter.h` | the mailbox both sides agree on. |
| `qspi-write.cfg` | the openocd script that drives it. |
| `qspi-write.sh` | the host side: builds, finds symbols, splits, runs openocd. |

The writer is a `BOOT_SRAM` build, and that is not a preference -- you cannot
write a flash chip you are executing from, and `QSPIHandle` refuses to try
(`CheckProgramMemory` -> `E_INVALID_MODE`).

Getting it running uses the same trick as `make sram`: let the Daisy bootloader
perform its whole handover and catch the resident app on its first instruction
with a hardware breakpoint, then overwrite SRAM underneath it. See
`../MultiEffect/sram-load.cfg` for why taking control any earlier does not
work.

After that, host and target talk through `QspiWriterReady()` -- a function
containing one `nop`, whose only purpose is to be a breakpoint. The writer
calls it whenever it wants work; the host's breakpoint catches it, reads the
last command's result out of the mailbox, writes the next command in, and
resumes. Every exchange is halt / write / resume, so nothing depends on reading
a *running* target, which is the part ST-Link's HLA transport is not dependable
at.

Payload goes across 32K at a time, into a buffer that lives in DTCMRAM.
`QSPIHandle` writes with a blocking `HAL_QSPI_Transmit`, so DTCMRAM is a
perfectly good source buffer -- it would not be for anything using DMA, which
cannot reach it. Each chunk is verified against the memory-mapped chip as soon
as it is written, while it is still in the buffer.

No address in any of this is written down twice. `qspi-write.sh` reads
`g_mailbox`, `QspiWriterReady` and the entry point out of the ELF with `nm` and
`readelf` and passes them to the Tcl script, so a rebuild that moves them
needs no edit anywhere.

## Finding the handover

The one genuinely awkward part. To catch the bootloader's jump you must know
where it is about to jump, and the authority on that is the reset vector of the
image resident in QSPI -- the word at `0x90040004`, which is literally what the
bootloader loads into PC. It is right for both kinds of resident image, a
`BOOT_SRAM` one pointing into `0x240xxxxx` and a `BOOT_QSPI` one pointing into
`0x900400xx`.

But QSPI is only mapped at `0x90000000` while something has configured the
peripheral: inside the bootloader's window, and again once an app has run its
own init. Not in between, and not when a debugger first attaches to a board
sitting at an openocd-loaded image -- which is this board's normal state.

Reading `0x24000004` instead, which is what `sram-load.cfg` used to do, is not
a substitute. That is the image *openocd* last loaded, not the one the
bootloader is about to load over the top of it.

So the script spends a reset of its own on finding out: let the board run, halt
it every 200ms until QSPI answers, read the vector, and only then start the
pass that catches the jump. Halting the bootloader is only dangerous if you go
on to force PC and SP; reading a word and resuming is not.

## What it needs

The Daisy bootloader in internal flash, and *some* valid app already in QSPI --
because the writer gets loaded by catching the bootloader's handover, and a
bootloader with nothing to jump to never hands over.
`../NewEffectDuino/flash.sh boot` installs the bootloader. The chicken-and-egg
case, a board with the bootloader and an empty QSPI, has not come up and would
need a different way in.

## What it shows

The LED after it finishes: slow blink for a clean run, fast for a failure. The
image only runs on the next reset, so that is what the board shows until it is
power-cycled.

On failure the script stops at the failing step and names it: a QSPI driver
error (with `QSPIHandle::Status` in `detail`), a verify mismatch (with the
offset of the first differing byte), or an out-of-range address.
