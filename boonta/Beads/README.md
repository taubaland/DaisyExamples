# Boonta Beads

A granular processor for the Boonta pedal, in the shape of Mutable
Instruments' Beads: a circular recording buffer with a character of its own,
a pool of grains with asymmetric envelopes and individual pitches, and a
freeze footswitch that simply stops the write head.

Built on the layout in `../Template`, so the model / controller / view / DSP
split and the execution-context rules are the same. Its sibling `../Clouds`
is the same idea with Clouds' decisions instead; what each one does
differently is set out under *What makes it Beads* below.

## Controls

| Control | Function |
|---------|----------|
| KNOB_1 | **Time** — how far back grains are drawn from, 20 ms to 20 s, exponential |
| KNOB_2 | **Size** — grain length, 10 ms to 1 s, exponential |
| KNOB_3 | **Pitch** — grain transposition, −2 to +2 octaves, centre is unity |
| KNOB_4 | **Density** — grain rate, 1 to 150 a second, and how clocked it is |
| KNOB_5 | **Shape** — grain envelope: percussive, triangle, reversed |
| KNOB_6 | **Mix** — dry to wet |
| TOG_SW_1 | **Seed** — per-grain scatter: down none, centre ±3 semitones, up ±12 |
| TOG_SW_2 | **Feedback** — down off, centre 0.45, up 0.85 back into the buffer |
| TOG_SW_3 | **Quality** — down clean, centre warm, up lo-fi (8 bit, quarter rate) |
| SW_SEL_1 / SW_SEL_2 | Previous / next pitch quantisation |
| SW_FS_1 | **Freeze** — momentary while held, latching on a long hold |
| SW_FS_2 | **Bypass** — latching, drives the relays |
| EXP | Pitch, once moved (`kExpressionTarget` in `Controls.cpp`) |
| MIDI | All of the above — CC 20-31 on USB or the DIN/TRS input |

Expression is on pitch here rather than on position, because with the select
buttons quantising it a foot on the pedal is a fretboard rather than a siren.
It has to be *moved* before it takes the parameter: nothing on the board
detects whether a pedal is plugged in -- the jack is an ADC channel and
nothing more -- so an expression target that claimed its parameter at boot
would pin the pitch knob to whatever an empty jack happens to read.

### The freeze footswitch

Identical to the Clouds build, deliberately — same gesture, same LED colour,
nothing to relearn between the two.

| Gesture | What happens |
|---------|--------------|
| Press | Freeze engages immediately |
| Release before 700 ms | It lets go — a tap is a stutter |
| Keep holding past 700 ms | The latch flips; freeze stays on when you step off |
| Long hold again | The latch flips back, and freeze drops at the moment it does rather than waiting for the release |
| Short tap *while latched* | Freeze momentarily **releases**: the buffer records again for as long as the switch is down |

That last row falls out of the exclusive-or in `Controls::ReadFootswitches()`
rather than being a special case, and it is the useful one: it is how you
punch a new phrase into a held drone.

| Freeze LED | Meaning |
|------------|---------|
| Dim blue | Buffer running |
| Pulsing blue | Frozen, momentarily — it will release when you step off |
| Steady blue | Frozen, latched |

### Pitch quantisation

The select buttons, which Beads hides in its alt settings. With the SEED
toggle scattering pitch by up to an octave, this is the difference between a
chord and a mess.

| Setting | LED | Grains land on |
|---------|-----|----------------|
| **Free** | pale blue | Anything |
| **Chromatic** | green | The nearest semitone |
| **Harmonic** | gold | Unison, fourths, fifths and octaves only |

Quantisation is applied *after* the scatter, so SEED on "up" with Harmonic
selected throws grains across two octaves and lands every one of them on a
consonant interval.

The selected setting's LED doubles as an output meter. The bypass LED is the
other instrument on the panel: it is white while the effect is in circuit and
tints red as the recording path approaches clipping. Beads earns a lot of its
sound from being driven, so that is a level to aim at rather than one to stay
under — but you want to know where it is.

## What makes it Beads

Same buffer, same grain pool and the same freeze as `../Clouds`. Three
decisions differ, and they are what the two pedals sound like.

**The grain envelope is asymmetric.** SHAPE sets how much of a grain is spent
rising: near zero a grain is all decay, which is a pluck; in the middle it is
a triangle; near one it is all rise, which is reversed tape. Clouds' window is
symmetric and SHAPE's opposite number there, TEXTURE, only decides how square
it is. One knob here covers percussive through to backwards.

**Every grain gets its own pitch.** The SEED toggle scatters pitch, pan and
start point per grain, and the quantiser snaps the result. Clouds transposes
the whole cloud together.

**The recording path has a character.** QUALITY drives the input into a soft
clipper and, at the top, reduces it to eight bits at a quarter of the sample
rate. That happens *in the buffer*, the way Beads does it, so what comes back
out is what was recorded rather than a filter applied on the way past — turn
it up, play a phrase, freeze, and the crunch is baked into the loop.

**DENSITY's randomness falls away gradually** across the whole sweep rather
than switching off at the centre the way the Clouds build does it, which
leaves a usable "loose but not random" region in the middle. That is where
most of Beads lives.

There is no reverb. Beads does not have one, and without Clouds' plate the
build fits internal flash with room to spare.

## MIDI

Every parameter and every switch is addressable, on USB and the DIN/TRS input
at once — whichever sends a message wins, and it does not matter which one it
came in on. Omni by default; set `midimap::kChannel` to listen to one channel.
The numbers line up with the Clouds build's wherever the two pedals have the
same kind of control, so one controller template drives both.

| CC | Controls |
|----|----------|
| 20–25 | Time, Size, Pitch, Density, Shape, Mix |
| 26 | Seed, scaled across the three positions |
| 27 | Feedback, scaled across the three positions |
| 28 | Quality, scaled across the three positions |
| 29 | Pitch quantisation, scaled across the three |
| 30 | Freeze (≥64 frozen) |
| 31 | Bypass (≥64 in circuit) |

Program Change 0–2 also selects the quantisation. The numbers live in 20–31,
a range the specification leaves undefined, so nothing collides with bank
select, modulation, volume or the channel mode messages — and unmapped
controllers are ignored rather than moving something at random.

### How a remote value survives a knob pointing somewhere else

A pot writes its parameter every block, so without something to stop it a
value sent over MIDI would last exactly one block. The rule, in
`Controls.cpp`:

- **A control gives way the moment anything else writes its parameter.**
  Noticing that needs no plumbing between the two controllers: each control
  remembers the value it last wrote, and if the model no longer holds that
  value then somebody else wrote it. It generalises for free — MIDI today,
  preset recall tomorrow — and costs one float compare per control.
- **It takes the parameter back when it is actually moved**, by 2% of travel
  from where it was standing when it was parked. Movement rather than mere
  disagreement, because a parked pot is almost always pointing somewhere
  other than where its parameter now is.

Toggles are discrete, so they need no threshold: the switch writes when it
changes position and not otherwise. Bypass and the quantise buttons are edge
driven and never fight a controller at all. Freeze is the one parameter with
two independent sources rather than one owner — the model ORs the
footswitch's flag with MIDI's, so a controller holding freeze cannot be
cancelled by the switch sitting up, and a controller cannot disable the
switch.

## How it works

Everything the pedal plays comes out of one circular buffer in SDRAM:
1048576 frames, about twenty-two seconds at 48 kHz, 16-bit stereo, four
megabytes. 16-bit because two dozen grains each make their own scattered read
every sample, and halving the bytes those reads pull out of SDRAM buys more
than the discarded mantissa bits are worth.

Audio is written into that buffer **whether or not the effect is in circuit**.
That is deliberate: the point of twenty-two seconds of history is that
stepping on the pedal has something to play, and a buffer that only filled
while engaged would start from silence every time.

**Freeze is the write head standing still.** Nothing else changes — the grains
have no idea it happened, they just keep reading a window that has stopped
being overwritten. That is also why feedback does nothing while frozen, and
why the clip LED goes dark: there is nothing being recorded.

Two implementation details earn their keep:

- **The read position is an integer index plus a fraction**, not one float. At
  the far end of a million-sample buffer a float has well under a sample of
  resolution left, which is audible as pitch drift on long grains.
- **The window is smoothstepped, not raised-cosine.** Three multiplies instead
  of a call into libm, for a curve that is indistinguishable at this length.

Grain levels are normalised by the square root of the overlap: grains land at
unrelated times, so they sum incoherently.

### Cost

Each live grain is two interpolated SDRAM reads a sample. `kMaxGrains` in
`Effect.h` is the ceiling — 24 — and is the first number to lower if a dense
cloud runs out of time in the callback. The reads are sequential within a
grain, so the D-cache does most of the work; it is the number of simultaneous
streams, not the rate, that eventually hurts.

## Files

| File | Role |
|------|------|
| `PedalState.h/.cpp` | Model — params, freeze, quantisation, telemetry |
| `Controls.h/.cpp` | Controller — knob table, toggle table, freeze machine, the pickup rule |
| `MidiControl.h/.cpp` | Controller — USB and DIN MIDI into the same model |
| `MidiMap.h` | Which CC does what |
| `LedView.h/.cpp` | View — freeze LED, clip LED, quantise LEDs and meter, relays |
| `Effect.h/.cpp` | The buffer, the recording path and the grain pool |
| `UsbDiag.h/.cpp` | USB clock trim and PHY ungate; see its header |
| `Beads.cpp` | Wiring only |
| `test/` | Host-side checks, no board needed |

Two deliberate exceptions to the template's one-way data flow:
`PedalState::SetOutputLevel()` and `SetInputPeak()` are written from the audio
callback so the LEDs can meter and warn. They are telemetry, not control —
nothing reads them back into the DSP — and the writes happen in `Beads.cpp`
rather than inside `Effect`, so the dependency stays visible and `Effect`
still only ever reads the model.

## Tests

`PedalState` and `Effect` know nothing about libDaisy, so they run on the
machine you are sitting at:

```bash
make test
```

What they are for is the two kinds of mistake a grain engine makes that you
cannot hear until you are standing in front of an amp — a parameter
combination that divides by zero, and a level that runs away. So most of it
is a sweep over every knob, toggle and quantisation with every sample checked
for being finite and bounded, plus:

- bypass passes through bit for bit;
- freeze keeps playing after the input stops, and stops the recording path;
- *without* freeze or feedback the cloud dies away, so nothing is stuck on;
- maximum feedback into maximum drive settles rather than climbing;
- SHAPE really is asymmetric — measured on DC, where the output *is* the
  envelope: 1.9% of a percussive grain is spent rising, 97.9% of a reversed
  one;
- the pitch map and the quantiser are right: +1 octave doubles the frequency,
  +2.4 semitones raises it by 1.149×, and Harmonic quantisation snaps that
  same setting back to exactly unity.

## On hardware

Flashed to a Boonta over ST-Link and inspected with gdb while running.
Confirmed there: the audio callback runs at 48 kHz with a block of 48 at a
480 MHz core clock; the ADC reaches the model and the toggles with it; and
USB MIDI enumerates as *Daisy Seed Built In* and drives every parameter.

**MIDI, verified end to end over USB.** Eight controllers sent from the host
and read back out of the model: all eight arrived and all eight were claimed.
Time landed on 0.787401557 for CC 20 = 100, which is 100/127 exactly; Seed
and Quality took high and low from 127 and 0; quantisation followed CC 29;
freeze engaged. The parameters that were sent nothing kept their pot values
rather than being reset — the pickup rule in both directions.

What the board cannot confirm from here is a pot *taking back* a parameter,
since that needs a hand on the knob. The parking half is confirmed: remote
values held for 67000 blocks against live pots.

**CPU load, measured.** Worst case means SIZE and DENSITY at maximum, PITCH
at +2 octaves, MIX fully wet, SEED and FEEDBACK up, QUALITY on lo-fi and
Harmonic quantisation — 24 grains live with the whole recording path
working. Averages wander by a few points run to run; the peaks are stable.

| Setting | Average | Peak |
|---------|---------|------|
| Bypassed | 1.6% | 1.7% |
| Pitch unity, clean, free quantise | 29.2% | 46.6% |
| **Worst case** | **41.7%** | **47.7%** |

Comfortable, and notably cheaper than the Clouds build's 86.8% worst-case
peak — the difference is almost entirely the plate that this pedal does not
have. Neither the lo-fi recording path nor harmonic quantisation costs
anything measurable: the first is a round and a counter per sample, the
second a scan of eleven floats per grain rather than per sample.

To measure it yourself, with the board running and `PROFILE_CPU` set (it is,
by default, in the `Makefile`):

```bash
arm-none-eabi-gdb build/Beads.elf -batch -ex 'target extended-remote :3333' -ex 'monitor halt' -ex 'print profile' -ex 'monitor resume' -ex 'detach'
```

`set var profile_reset = 1` clears the worst-block figure without rebooting,
so a measurement can be scoped to one setting. Give it ten seconds or more
after a change before believing the peak — a settings change produces one
expensive block, and reading immediately afterwards reports it as the peak.

## Build

```
make
```

Adding USB MIDI took the build past what the 128K internal flash can hold
alongside the bootloader, so `APP_TYPE = BOOT_SRAM` is now set rather than
commented out: 110736 B, 22.5% of the 480K SRAM region. SDRAM is 4 MB —
6.25% of the 64 MB — which no app type touches.

Three ways to get it onto a board, one of them new:

```bash
make boot
```

Puts the Daisy bootloader in internal flash over the ST-Link. Once, ever.

```bash
make program-dfu
```

Writes the app into QSPI, with the board in bootloader mode. This is the one
that survives a power cycle.

```bash
make sram
```

Loads straight into SRAM over the ST-Link — about a second, no flash wear,
and gone on the next power cycle. This is what the measurements above were
taken on, because it leaves whatever is in QSPI alone. It needs the
bootloader in internal flash and *some* valid app already in QSPI.

`sram-load.cfg` here carries a fix the copies in `../MultiEffect` and
`../NewEffectDuino` still need: it sets VTOR explicitly rather than
inheriting it from the bootloader. See the same section of
[`../Clouds/README.md`](../Clouds/README.md) for what goes wrong otherwise.
