# Flashing the Boonta

How to get firmware from this repository onto the pedal. It applies to every
firmware here that is built to run through the Daisy bootloader: **Beads**,
**Clouds**, **CloudsSecrets**, **Elements**, **MultiEffect** and
**NewEffectDuino**.

Commands are for **Git Bash on Windows**. Where PowerShell differs, it says so.

## 1. How the pedal boots

The Daisy Seed (STM32H750) has only 128 KB of internal flash, and these
programs are bigger. So the board holds two things:

| Where | What | Written how |
|---|---|---|
| Internal flash, `0x08000000` | The **Daisy bootloader**. Never changes. | `make boot` |
| QSPI flash, `0x90040000` | The **firmware**. | `qspi-write.sh` |

At power-on the bootloader waits about two seconds, copies the firmware from
QSPI into RAM, and jumps to it. Both pieces are needed. Swapping firmwares
means rewriting the QSPI part only.

**DFU does not work on this board.** The usual Daisy route (`make program-dfu`,
bootloader mode, `dfu-util`) never enumerates here. Everything below goes over
the **ST-Link** instead. `QspiWriter/README.md` has the evidence.

Do not run `make program` in a firmware folder either. It writes straight to
internal flash, would overwrite the bootloader, and is refused for these
builds anyway. (`RelayTest` and the other small test programs are internal-flash
programs; flashing one *replaces the bootloader*, so run `make boot` again
before going back to a QSPI firmware.)

## 2. What you need

### Hardware

- The pedal, **powered** (USB-C from the PC is enough).
- An **ST-Link** probe on the Daisy's SWD pins: SWDIO, SWCLK, GND and 3V3 as
  the probe's voltage reference.

Audio cables, MIDI and the footswitches do not need to be connected. The
Daisy's own USB port is not used for flashing. Do not run the pedal's power
supply and USB at the same time unless you know the board isolates them.

### Software

- **Daisy Toolchain for Windows** installed in `C:\Program Files\DaisyToolchain`.
  It supplies `arm-none-eabi-gcc`, `openocd` and `arm-none-eabi-gdb`. The
  scripts look for openocd at
  `/c/Program Files/DaisyToolchain/bin/openocd.exe`, with its scripts in
  `/c/Program Files/DaisyToolchain/openocd/scripts`. If yours is elsewhere, set
  `OCD` and `OCD_DIR` (see Troubleshooting).
- The **ST-Link driver**, so openocd can see the probe.
- **Git Bash**, for `make`, `sh` and the scripts.
- **Python**, on the path, for Elements only (it splits out the sample data).
- **Clang**, only if you want `make test`.

## 3. Getting the code

Clone it. Do not use GitHub's "Download ZIP": a ZIP has no `.git`, and its
submodule folders (`libDaisy`, `DaisySP`) arrive empty.

```bash
git clone --recurse-submodules -b boonta-midi-presets https://github.com/taubaland/DaisyExamples.git
cd DaisyExamples
```

Build the two libraries once:

```bash
cd libDaisy && make -j8 && cd ..
cd DaisySP && make -j8 && cd ..
```

## 4. Build a firmware

From the repository root, in the firmware's folder:

```bash
cd boonta/Beads
make
```

| Folder | Output to flash |
|---|---|
| `boonta/Beads` | `build/Beads.bin` |
| `boonta/Clouds` | `build/Clouds.bin` |
| `boonta/CloudsSecrets` | `build/CloudsSecrets.bin` |
| `boonta/Elements` | **`build/Elements-full.bin`**, not `Elements.bin` |
| `boonta/MultiEffect` | `build/MultiEffect.bin` |

Elements is different: its sample tables are appended to the program, and
`Elements-full.bin` has them. `Elements.bin` alone is the program without them.

To build everything:

```bash
cd boonta
for d in Beads Clouds CloudsSecrets Elements MultiEffect; do (cd $d && make -j8) || break; done
```

`make test` in a folder runs its host-side tests. `make clean` removes `build/`.

## 5. Put the bootloader on the board (once)

Only needed on a board that has no bootloader, or after something has
overwritten internal flash (for example a `RelayTest` flash). With the ST-Link
connected:

```bash
cd boonta/Beads
make boot
```

This writes `libDaisy/core/dsy_bootloader_v6_4-intdfu-2000ms.bin` to
`0x08000000`, verifies it, and resets the board. It ends with "Verified OK".
It overwrites internal flash. Any firmware folder's `make boot` does the same
thing.

If openocd cannot find its scripts, pass the directory:

```bash
make boot OCD_DIR="C:/Program Files/DaisyToolchain/openocd/scripts"
```

## 6. Write the firmware to QSPI

From `boonta/`:

```bash
cd boonta
./QspiWriter/qspi-write.sh Beads/build/Beads.bin
```

Swap the file for the firmware you want. For Elements:

```bash
./QspiWriter/qspi-write.sh Elements/build/Elements-full.bin
```

From PowerShell:

```powershell
cd D:\path\to\DaisyExamples\boonta
& "C:\Program Files\Git\bin\bash.exe" ./QspiWriter/qspi-write.sh Beads/build/Beads.bin
```

(Use Git's `bash.exe` by its full path. A bare `bash` in PowerShell may be
WSL's, which does not work with this script.)

What it does: builds a small flash-writer program the first time, runs it on
the pedal under openocd, erases the QSPI sectors it needs, writes the file in
32 KB chunks, and verifies each one. It takes a minute or so and ends with:

```
written and verified; the board is blinking slowly. Power-cycle to run it.
```

The firmware goes to `0x90040000`, where the bootloader looks for it. A second
argument writes elsewhere; you will not normally need it.

This **overwrites whatever firmware was in QSPI**. There is no undo, so if the
old image matters, keep its `.bin`.

## 7. Run it

Unplug the ST-Link and **power-cycle** the pedal (the writer is still running
from RAM until you do). The bootloader's LED pulses for about two seconds,
then the firmware starts.

Every firmware here starts **bypassed**: the right footswitch LED is off and
your signal passes straight through. Press the right footswitch to put the
effect in circuit.

## 8. Trying a build without writing flash

For testing, loading a build straight into RAM saves a flash write. It is gone
at power-off.

| Command | Use when |
|---|---|
| `make sram` | The bootloader is installed and some valid firmware is already in QSPI. |
| `make sram-bare` (Elements) | The board has no bootloader and nothing of ours in QSPI. |

`make sram` depends on the same handover as the writer, so it does **not** work
on a board with blank QSPI. Write a firmware with `qspi-write.sh` first.

## 9. Blank or new boards

On a board with blank QSPI, or no bootloader, the writer has nothing to catch
the bootloader's jump into. It then starts itself directly from RAM, which is
what the bootloader would have done. You will see:

```
No app in QSPI to catch the bootloader jump into; starting the
writer directly instead.
```

That is expected. To force that path on any board:

```bash
QSPI_BARE=1 ./QspiWriter/qspi-write.sh Beads/build/Beads.bin
```

So the order for a brand-new board is: `make boot`, then `qspi-write.sh`, then
power-cycle.

## 10. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `fatal: not a git repository` on `git submodule update` | The folder came from a ZIP. Clone with `--recurse-submodules` as in section 3. |
| `make` cannot find libDaisy or DaisySP | The submodules are missing or not built. Section 3. |
| `make boot` or the writer cannot find openocd's scripts | Set `OCD_DIR` (and `OCD` for the writer) to your DaisyToolchain's. Example: `OCD_DIR="C:/Program Files/DaisyToolchain/openocd/scripts"`. |
| openocd cannot see the probe | ST-Link driver missing, cable loose, or another program (a debugger, STM32CubeProgrammer) holds the probe. |
| `target stm32h7x.cpu0 examination failed` | Repeated failed attempts can leave the chip unexaminable. Unplug the power and the ST-Link, wait a few seconds, plug both back in. `NewEffectDuino/PORTING.md` has the longer recovery. |
| `Could not read a reset vector out of QSPI at 0x90040004` | Old version of the writer on a board with blank QSPI. Update to the current `qspi-write.cfg`; it now falls back to a direct start. |
| `Never reached the app entry` from `make sram` | QSPI has no app to hand over from. Use `qspi-write.sh`, not `make sram`. |
| Written and verified, but the pedal does nothing at power-on | No bootloader in internal flash (run `make boot`), or you did not power-cycle. Without a bootloader, nothing copies QSPI into RAM. |
| Elements: sampled exciters silent, two LEDs blink red | You wrote `Elements.bin`, not `Elements-full.bin`. Write the full image. |
| Strange behaviour after switching firmware | Each firmware keeps its own saved settings and ignores the others', so it starts at its defaults. |
| The writer fails partway | Power-cycle the board and run it again. The writer erases and rewrites the whole span each time. |

## 11. Quick reference

```bash
# once per clone
git clone --recurse-submodules -b boonta-midi-presets https://github.com/taubaland/DaisyExamples.git
(cd DaisyExamples/libDaisy && make -j8)
(cd DaisyExamples/DaisySP  && make -j8)

# once per board
cd DaisyExamples/boonta/Beads && make boot

# every time
cd DaisyExamples/boonta/Beads && make
cd .. && ./QspiWriter/qspi-write.sh Beads/build/Beads.bin
# then unplug the ST-Link and power-cycle the pedal
```
