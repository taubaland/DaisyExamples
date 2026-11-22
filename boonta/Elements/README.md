# Boonta Elements

Mutable Instruments' Elements on the Boonta pedal: a modal synthesis voice —
bow, blow and strike exciters into a resonator, then a reverb — with the
pedal's input jack as a fourth exciter.

The synthesis is Emilie Gillet's code, vendored under
[`elements/`](elements). Everything else here is the part of the module that
was hardware: the job a panel of pots and CV jacks did, done instead for six
knobs, two footswitches and a MIDI cable.

Built on the layout in `../Template` and `../Clouds`, so the model /
controller / view / DSP split and the execution-context rules are the same.

## Status

| | |
|---|---|
| Builds | yes — 165 KB program, 34% of what the bootloader will load |
| Host tests | pass (`make test`) |
| Runs on the pedal | yes, loaded into RAM with `make sram-bare`; no faults |
| CPU, measured | 42% worst case — see below |
| **Heard by anyone** | **no** |
| Installed | yes: bootloader in internal flash, `Elements-full.bin` in QSPI at `0x90040000`. Boots on its own, samples checksum and load from QSPI, audio callback runs, no faults |
| MIDI | builds; not exercised on hardware. USB now enumerates as "Daisy Seed Built In" after the reflash, but no message has been sent |

Everything about how it *sounds* and how the controls *feel* — the input
trigger thresholds above all — is a first guess that has passed tests and has
not met an instrument.

## Controls

Elements has eighteen parameters worth a knob. The pedal has six, so the
knobs are paged, and the three select LEDs are the three pages.

| Page | LED | KNOB_1 | KNOB_2 | KNOB_3 | KNOB_4 | KNOB_5 | KNOB_6 |
|---|---|---|---|---|---|---|---|
| Resonator | teal | Pitch | Geometry | Brightness | Damping | Position | Space |
| Exciters | amber | Contour | Bow | Blow | Strike | Input | Strength |
| Colour | violet | Bow timbre | Flow | Blow timbre | Mallet | Strike timbre | Fine |

| Control | Function |
|---------|----------|
| SW_SEL_1 / SW_SEL_2 | Previous / next page |
| TOG_SW_1 | **Resonator** — down modal, centre string, up a chord of five strings |
| TOG_SW_2 | **Trigger** — down manual, centre the input's own attacks, up drone |
| TOG_SW_3 | **Input to** — down strike (straight into the resonator), centre both, up blow (through the diffuser) |
| SW_FS_1 | **Gate** — hold to play; hold past 700 ms to latch |
| SW_FS_2 | **Bypass** — latching, drives the relays |
| EXP | Geometry, once moved |
| MIDI | Notes play it; CCs reach everything. See below. |

**Pitch** is quantised to semitones over four octaves, C1 to C5. With the
trigger toggle at centre it is instead a transposition: centre is unison, two
octaves down to two up. **Fine** is
a semitone either side. **Space** is stereo width up to about a third of
travel and reverb from the middle up; right at the bottom it lets the raw
exciter through. **Blow** past two thirds brings in the tube; **Strike** past
0.8 bleeds the raw strike into the output. Those laws are upstream's.

In the chord position, **Geometry** picks the chord rather than the shape.

### Paging without jumps

After a page turn every pot is pointing at a value that belongs to the
parameter it just left. So turning the page *parks* all six: nothing changes
until a knob is turned at least 2% of travel and to within 10% of travel of
the value its new parameter holds, at which point it picks the parameter up
and tracks it. This is the pickup rule Clouds uses to share a parameter with
MIDI, used for a second purpose; there is one pickup per parameter rather
than per pot, so coming back to a page finds it as you left it.

Only the page that is up at boot takes its values from the panel. The other
twelve parameters start from defaults that are a playable patch on their own
(Elements' own init patch: a struck bar, no bow, no breath), or from saved
state.

### What plays a note

Three things can, and they are ORed — none can cancel another:

- **The gate footswitch.** Press for a note, hold past 700 ms and it latches;
  while latched, a tap drops the gate for as long as the switch is down and
  re-strikes on release.
- **MIDI**: a held key, or CC 30.
- **TOG_SW_2**: centre, the input's attacks, with the resonator tuned to the pitch it hears; up, the gate is held open.

With the trigger toggle at centre the pedal **follows the guitar**: a note
opens the gate, silence closes it, and a new attack over the tail of the last
one drops the gate for 2 ms so the exciters see an edge. It also tracks the
guitar's pitch (`PitchTracker.h`) and tunes the resonator to it, so each note
you play rings the resonator at that note, plus whatever partials the
resonator adds: the modal model's inharmonic overtones, or the five tones of a
chord in the strings model. Pitch knob transposes, so a fifth or an octave up
is a setting, not a retune.

The tracker is McLeod's pitch method on a 32 ms window: reliable from the low
E (82 Hz) to about 1 kHz, and it reports whole semitones with hysteresis wide
enough to sit through vibrato. A new note is first seen about 30 ms after its
attack (longer on the low strings), so the exciters strike the old pitch
first and the pedal re-strikes at the new one once it knows. It is
monophonic: chords confuse it, and a chord strummed will track whichever
note is strongest. Between notes, and on silence or noise, it holds the last
note. The gate and the tracker both read the jack before the Input knob, so
Input at zero still follows the guitar without exciting the resonator
directly.

The thresholds are at the top of `Effect.cpp` and `PitchTracker.h` and are the
numbers most likely to need changing once someone plays through it.

Whatever the toggle says, the input always *excites* the resonator at the
level of the **Input** knob. Turn Input down to play it as a synth; turn Bow,
Blow and Strike down and the trigger to manual to use it purely as a
resonator for the guitar.

### LEDs

| LED | Meaning |
|-----|---------|
| Gate footswitch, dim amber | Nothing playing |
| — pulsing | A note you are holding; it stops when you step off |
| — steady, bright | Latched (or held by a controller) |
| — steady, half | A note the input or the drone position is playing |
| Bypass footswitch, white | In circuit |
| Selected page LED | Its page colour, and an output meter |
| **Other two page LEDs blinking red** | **The sample data is not in flash.** See *Sample data*. |

## MIDI

On USB and the DIN/TRS input at once. Omni by default; set
`midimap::kChannel` to listen to one channel.

| Message | Does |
|---|---|
| Note on / off | Sets the pitch and holds the gate. Last note wins; the gate drops when the last key comes up. |
| Velocity | Scales Strength, while a key is down |
| Pitch bend | ± 2 semitones |
| CC 102–107 | Pitch, Geometry, Brightness, Damping, Position, Space |
| CC 108–113 | Contour, Bow, Blow, Strike, Input, Strength |
| CC 114–119 | Bow timbre, Flow, Blow timbre, Mallet, Strike timbre, Fine |
| CC 20 / 21 / 22 | Resonator / Trigger / Input routing, across the three positions |
| CC 23, Program Change 0–2 | Knob page |
| CC 30 | Gate (≥ 64 held) |
| CC 31 | Bypass (≥ 64 in circuit) |
| CC 120–127 | Releases all held notes |

A key transposes around middle C: with C4 held the Pitch knob means what it
says, and the last key played stays in force after it is released, so the
footswitch replays it.

## How it is put together

```
input jack -> gain -> noise gate -+-> strike in  (straight to the resonator)
                                  +-> blow in    (through the diffuser)
                                  +-> envelope follower -> gate

gate, note, strength -> bow / blow / strike -> resonator -> spread -> reverb
```

**32 kHz.** Elements is written for 32 kHz and its lookup tables are computed
for that rate, so the codec is run at 32 kHz rather than the tables being
regenerated. The audio callback is 32 samples, a thousand a second, which is
the rate the board tunes its knob smoothing for; `Effect` feeds the Part in
two chunks of sixteen, the most it accepts.

**Upstream is untouched, with one line changed.** `elements/dsp/` and
`elements/resources.*` are pichenettes/eurorack at `f4cfa76`, except for the
resonator's mode count in `voice.cc` (below), which is marked `BOONTA`. Three
things keep the rest identical:

- `ElementsDsp.cpp` `#include`s the `.cc` files, because libDaisy's Makefile
  only builds `.cpp`.
- `elements/drivers/debug_pin.h` is an empty stand-in for an STM32F4 header
  that `resonator.cc` includes and does not use. It is the one file under
  `elements/` that is not upstream's, and says so.
- The Part is given zeroed memory. Upstream keeps it in `.bss` and several
  members are never otherwise initialised; un-zeroed, it faults inside
  `Tube::Process`.

**Memory.** The Part is 113 KB and its reverb line 64 KB. A `BOOT_SRAM` build
keeps its variables in the 128 KB DTCMRAM, so those two come off the heap,
which is in the D2 SRAM, once at boot.

**64 resonator modes, not 52.** Upstream runs 52 of a possible 64 because
that was what an STM32F4 could afford ("runs with 56 extremely tightly").
`Voice::Init()` sets it and the resonator is private to `Voice`, so the
only way to change it is the one line in `elements/dsp/voice.cc`, which now
says 64 and keeps the original beside it. Re-vendoring upstream will undo it.
It cost about 0.4 points of average and 4 of worst-case CPU in the modal
model (30.1% / 34.2%); modes above the Nyquist limit are skipped, so low
notes pay for more of them than high ones.

### Sample data

The strike and blow exciters play recordings: 338 KB of 16-bit audio in two
tables. The bootloader copies at most 480 KB into RAM and the rest of the
program is 165 KB, so they would only just not fit — and running in place
from QSPI instead would cost the speed and the ability to save settings to
the same chip. So the tables travel beside the program instead of in it:

- `ElementsResources.cpp` renames them as it compiles `resources.cc`, so the
  program refers to nothing in that object and the linker drops them.
- The Makefile cuts them out of the object file into `build/samples.bin`,
  with a header and checksum, and appends that to the program 512 KB in —
  past anything the bootloader copies. The result is
  **`build/Elements-full.bin`, the one file to write to QSPI.**
- The linker is told the tables live at `0xC3F00000`, the last megabyte of
  SDRAM, and `Samples::Load()` copies them there at boot after checking them.

If the blob is missing or damaged, the tables are zeroed: the sampled
exciters are silent, everything else works, and two page LEDs blink red.

`make program-dfu` writes `Elements.bin`, the program alone. That is enough
after the full image has been written once, since the blob does not change.

## CPU

Measured on the pedal with every exciter at whatever the panel had, the gate
held and the reverb up, running from RAM at 32 kHz:

| Resonator | Average | Worst block |
|---|---|---|
| Modal | 30.1% | 34.2% |
| Modal, following a guitar | 37.5% | 41.7% |
| String | 11.7% | 11.9% |
| Strings (chord) | 20.5% | 20.7% |
| Bypassed | 0.5% | 0.6% |

The reverb runs whether or not Space asks for any, so there is no cheaper
setting hiding in there. Bypassed, `Effect` renders nothing.

To measure it yourself: bypassed, the load reads as nothing, and taking the
pedal out of bypass from a debugger closes the relays and sends whatever it
is playing to the amp. So there is a hook. With the board running:

```
(gdb) set var profile_render = 7     # render though bypassed; hold the gate; synthetic guitar
(gdb) set var profile_reset = 1
(gdb) print profile
(gdb) set var profile_render = 0
```

Bit 0 renders from a copy of the model with bypass cleared and throws the
audio away, so the relays never move; bit 1 holds the gate in that copy; bit
2 replaces the jack with a 196 Hz sawtooth so the pitch tracker has work to
do (on a silent jack it skips its analysis). The tracker's analysis is
spread across callbacks; done in one go it took ~2.8 ms against a 1 ms block.

## Build

```
make            # build/Elements.bin, build/samples.bin, build/Elements-full.bin
make test       # host checks, no hardware
```

Needs Python on the path for `tools/samples.py`.

### Getting it onto a board

**Into RAM, with nothing installed: `make sram-bare`.** This is what the
measurements above were taken with. It needs no bootloader and writes no
flash: openocd does what the bootloader would have (FPU on, D2 SRAM clocks
on, `boot_info.version`, VTOR), starts the program, waits for it to bring
SDRAM up, and then loads the sample tables into SDRAM itself.
[`sram-bare.cfg`](sram-bare.cfg) explains each step. Gone on the next reset.

**To stay: write `build/Elements-full.bin` to QSPI at `0x90040000`**, with the
Daisy bootloader in internal flash (`make boot`). On this board DFU into QSPI
has not been reliable, so that means `../QspiWriter`:

```
../QspiWriter/qspi-write.sh build/Elements-full.bin
```

This has been done on the development board (backups of the previous QSPI
and internal-flash contents were taken first). It overwrites whatever is in
QSPI, and `make boot` overwrites whatever is in internal flash.

For players, see [`MANUAL.md`](MANUAL.md).

**Into RAM, with the bootloader installed: `make sram`**, as for Clouds. The
sample tables come from QSPI as usual, so the full image has to have been
written once.

## Tests

`make test` builds the model, `Effect` and upstream's DSP for the host and
checks:

- the model's rules: paging, note handling, the three gate sources, and that
  saved state from another firmware or with a bad value is refused whole;
- that the defaults are a playable patch, and bypass renders exactly nothing;
- a sweep of every parameter at both extremes in every model and routing with
  the input driven hard — 972 cases, every sample finite and within full
  scale. This is aimed at the mapping layer handing upstream an index one
  past the end of a table, which is the one way this code can hurt that code;
- pitch quantisation, and the input trigger: one gate edge per pluck for
  plucks arriving over each other's tails.

`Controls`, `LedView`, `MidiControl` and `Samples` touch libDaisy and are not
covered. The paging pickup in particular is tested only by reading it.

## Not done

- **Nobody has listened to it.**
- `Persist` has not been seen saving or restoring on hardware. (It is
  Clouds' file unchanged, and shares its flash sector; each firmware refuses
  the other's settings.)
- MIDI and the paging pickup have not been exercised on hardware.
- No output level control and no dry signal: all eighteen knob slots went to
  Elements' own parameters.
- The easter egg (the "ominous voice") is compiled in and not reachable.
- The reverb freeze upstream reaches by pushing Space past its panel range
  with CV is not reachable.

## Licence

Everything under `elements/` except `drivers/debug_pin.h` is © Emilie Gillet,
MIT — the notices are in the files. `stmlib` likewise.
