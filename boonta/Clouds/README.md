# Boonta Clouds

A granular processor for the Boonta pedal, in the shape of Mutable
Instruments' Clouds: a circular recording buffer, a pool of grains reading
windows out of it, and a freeze footswitch that simply stops the write head.

Built on the layout in `../Template`, so the model / controller / view / DSP
split and the execution-context rules are the same. What follows is what is
different.

## Controls

| Control | Function |
|---------|----------|
| KNOB_1 | **Position** — how far back in the buffer grains are drawn from, 0 to ~11 s |
| KNOB_2 | **Size** — grain length, 16 ms to 2 s, exponential |
| KNOB_3 | **Pitch** — grain transposition, −2 to +2 octaves, centre is unity |
| KNOB_4 | **Density** — grain rate, and how irregular the spacing is |
| KNOB_5 | **Texture** — grain window, near-rectangular to all curve |
| KNOB_6 | **Mix** — dry to wet |
| TOG_SW_1 | **Spread** — down mono, centre wide, up full stereo scatter |
| TOG_SW_2 | **Feedback** — down off, centre 0.45, up 0.8 back into the buffer |
| TOG_SW_3 | **Reverb** — down off, centre blended, up drenched |
| SW_SEL_1 / SW_SEL_2 | Previous / next playback mode |
| SW_FS_1 | **Freeze** — momentary while held, latching on a long hold |
| SW_FS_2 | **Bypass** — latching, drives the relays |
| EXP | Position, once moved (`kExpressionTarget` in `Controls.cpp`) |
| MIDI | All of the above — CC 20–31 on USB or the DIN/TRS input |

Expression has to be *moved* before it takes its parameter. Nothing on the
board detects whether a pedal is plugged in — the jack is an ADC channel and
nothing more — so an expression target that claimed its parameter at boot
would pin that knob to whatever an empty jack happens to read.

### The freeze footswitch

One switch, two gestures, and you never have to decide which one you are
making until after you have made it:

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

The LED says which state you are in, and the distinction that matters on
stage is latched from held — a latched freeze will still be there when you
take your foot off:

| Freeze LED | Meaning |
|------------|---------|
| Dim blue | Buffer running |
| Pulsing blue | Frozen, momentarily — it will release when you step off |
| Steady blue | Frozen, latched |

### Playback modes

The three select LEDs are the playback mode, not presets. All three share one
buffer and one grain pool; they differ only in how the scheduler picks start
points.

| Mode | LED | Behaviour |
|------|-----|-----------|
| **Granular** | violet | Random start points, random spacing, random panning. The cloud. |
| **Stretch** | teal | One playhead with two grains crossfading over it. DENSITY becomes playback rate: stopped at the bottom, real time at the centre, double speed at the top. |
| **Delay** | amber | The same crossfade with the playhead locked to real time, so at unity PITCH it is a plain delay and off unity a transposing one. POSITION is the delay time; DENSITY damps the feedback path, which has nothing else to do here. Freeze turns it into a looper. |

The selected mode's LED doubles as an output meter, which is the quickest way
to tell a buffer with nothing in it from a mix knob that is turned down.

## MIDI

Every parameter and every switch is addressable, on USB and the DIN/TRS input
at once — whichever sends a message wins, and it does not matter which one it
came in on. Omni by default; set `midimap::kChannel` to listen to one channel.

| CC | Controls |
|----|----------|
| 20–25 | Position, Size, Pitch, Density, Texture, Mix |
| 26 | Spread, scaled across the three positions |
| 27 | Feedback, scaled across the three positions |
| 28 | Reverb, scaled across the three positions |
| 29 | Playback mode, scaled across the three |
| 30 | Freeze (≥64 frozen) |
| 31 | Bypass (≥64 in circuit) |

Program Change 0–2 also selects the playback mode. The numbers live in 20–31,
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
changes position and not otherwise. Bypass and the mode buttons are edge
driven and never fight a controller at all. Freeze is the one parameter with
two independent sources rather than one owner — the model ORs the
footswitch's flag with MIDI's, so a controller holding freeze cannot be
cancelled by the switch sitting up, and a controller cannot disable the
switch.

## How it works

Everything the pedal plays comes out of one circular buffer in SDRAM:
524288 frames, about eleven seconds at 48 kHz, 16-bit stereo, two megabytes.
16-bit for the same reason Clouds does it — two dozen grains each make their
own scattered read every sample, and halving the bytes those reads pull out
of SDRAM buys more than the discarded mantissa bits are worth.

Audio is written into that buffer **whether or not the effect is in circuit**.
That is deliberate: the point of eleven seconds of history is that stepping on
the pedal has something to play, and a buffer that only filled while engaged
would start from silence every time.

**Freeze is the write head standing still.** Nothing else changes — the grains
have no idea it happened, they just keep reading a window that has stopped
being overwritten. That is also why feedback does nothing while frozen: there
is nowhere for it to be written.

A grain is a short window into the buffer, read at its own rate and faded in
and out. Two details earn their keep:

- **The read position is an integer index plus a fraction**, not one float. At
  the far end of a half-million-sample buffer a float has about a fifth of a
  sample of resolution left, which is audible as pitch drift on long grains.
- **The window is smoothstepped, not raised-cosine.** Three multiplies instead
  of a call into libm, and `s(x) + s(1−x)` is exactly 1 — which is what lets
  Stretch and Delay crossfade two grains with no ripple in the level, by
  setting the hop to `length × (1 − ramp)`.

Grain levels are normalised by the square root of the overlap in Granular
mode, where grains land at unrelated times and sum incoherently, and left at
unity in the two fixed-overlap modes, where they sum to exactly one.

### Cost

Each live grain is two interpolated SDRAM reads a sample, so the bill is set
by `kMaxGrains` in `Effect.h` — 24 — and by the reverb. Measured on the board
rather than estimated; see **On hardware** below. The short version is that
the pedal has headroom everywhere except one corner, and in that corner it
has about 13%.

## Files

| File | Role |
|------|------|
| `PedalState.h/.cpp` | Model — params, freeze, mode, output telemetry |
| `Controls.h/.cpp` | Controller — knob table, toggle table, freeze machine, the pickup rule |
| `MidiControl.h/.cpp` | Controller — USB and DIN MIDI into the same model |
| `MidiMap.h` | Which CC does what |
| `LedView.h/.cpp` | View — freeze LED, bypass LED, mode LEDs and meter, relays |
| `Effect.h/.cpp` | The buffer, the grain pool and the three schedulers |
| `Reverb.h/.cpp` | The plate at the end of the chain |
| `UsbDiag.h/.cpp` | USB clock trim and PHY ungate; see its header |
| `Clouds.cpp` | Wiring only |
| `test/` | Host-side checks, no board needed |

One deliberate exception to the template's one-way data flow:
`PedalState::SetOutputLevel()` is written from the audio callback with the
peak the DSP just produced, so the LEDs can meter it. It is telemetry, not
control — nothing reads it back into the DSP — and the write happens in
`Clouds.cpp` rather than inside `Effect`, so the dependency stays visible and
`Effect` still only ever reads the model.

## Tests

`PedalState`, `Effect` and `Reverb` know nothing about libDaisy, so they run
on the machine you are sitting at:

```bash
make test
```

What they are for is the two kinds of mistake a grain engine makes that you
cannot hear until you are standing in front of an amp — a parameter
combination that divides by zero, and a level that runs away. So most of it
is a sweep over every mode, toggle and knob with every sample checked for
being finite and bounded, plus:

- bypass passes through bit for bit;
- freeze keeps playing after the input stops;
- *without* freeze or feedback the cloud dies away, so nothing is stuck on;
- maximum feedback into maximum reverb settles rather than climbing;
- the pitch map is right — +1 octave doubles the measured frequency.

One thing the tests make obvious that is easy to miss on the bench: a freshly
powered granulator reads silence until the buffer has been primed, because
even at POSITION zero a grain starts one grain length behind the write head.

## On hardware

Flashed to a Boonta over ST-Link and inspected with gdb while running.
Confirmed there: the audio callback runs at 48 kHz with a block of 48 at a
480 MHz core clock; the ADC reaches the model and the toggles with it; and
USB MIDI enumerates as *Daisy Seed Built In* and drives every parameter.

**MIDI, verified end to end over USB.** Nine controllers sent from the host
and read back out of the model: all nine arrived and all nine were claimed.
Position landed on 0.503937 for CC 20 = 64, which is 64/127 exactly; the
three toggles took low, mid and high from 0, 64 and 127; mode followed both
CC 29 and a Program Change; freeze engaged and released. Two negatives worth
as much as the positives: a modulation message (CC 1) arrived and was *not*
claimed, and the three parameters that were sent nothing kept their pot
values rather than being reset — the pickup rule in both directions.

What the board cannot confirm from here is a pot *taking back* a parameter,
since that needs a hand on the knob. The parking half is confirmed: remote
values held for 67000 blocks against live pots.

**CPU load, measured.** Worst case means SIZE and DENSITY at maximum, PITCH
at +2 octaves, MIX fully wet and every toggle up, which is 24 grains live
with the plate running. Averages wander by about ±4 points run to run; the
peaks are stable to within a point.

| Setting | Average | Peak |
|---------|---------|------|
| Bypassed | 2.5% | 2.5% |
| Delay mode, worst | 26.3% | 33.7% |
| Stretch mode, worst | 28.2% | 35.9% |
| Granular, worst, reverb off | 46.5% | 55.7% |
| **Granular, worst, everything up** | **73.5%** | **86.8%** |
| Granular, worst, with `kMaxGrains = 16` | 50.9% | 68.2% |

Three things that fall out of those rows:

- **The reverb is about 30 points of peak** — more than the entire delay
  mode. It is twelve delay lines in SDRAM and it is paying for the traffic,
  not the arithmetic.
- **Pitching up costs roughly as much again as the grains themselves.** At
  unity the same granular setting sits near 26% average; at +2 octaves it is
  near 47%. The grain read strides four frames instead of one, so a cache
  line serves a quarter as many samples.
- **Grain cost is linear in the pool size**, at about 2.4 points of peak per
  grain. Dropping `kMaxGrains` to 16 buys 19 points of peak and is the lever
  to pull if 13% of headroom is not enough for you. It is left at 24 because
  that is the voicing, and because nothing overran at 24.

Two things that were tried and did **not** help, recorded so they are not
tried twice. Replacing DaisySP's `DelayLine` in the reverb with an exact
integer line, on the theory that its second interpolation fetch was wasted
SDRAM traffic: no change, because the two samples are adjacent and the
second is already in the cache line the first pulled in. And reading a
single measurement as fact: run-to-run spread on the average is several
points, and a mode change produces one expensive block that will show up as
a 75% peak in stretch mode if you measure immediately after switching.

To measure it yourself, with the board running and `PROFILE_CPU` set (it is,
by default, in the `Makefile`):

```bash
arm-none-eabi-gdb build/Clouds.elf -batch -ex 'target extended-remote :3333' -ex 'monitor halt' -ex 'print profile' -ex 'monitor resume' -ex 'detach'
```

`set var profile_reset = 1` clears the worst-block figure without rebooting,
so a measurement can be scoped to one setting. Give it ten seconds or more
after a change before believing the peak.

## Build

```
make
```

Adding USB MIDI took the build past what the 128K internal flash can hold
alongside the bootloader, so `APP_TYPE = BOOT_SRAM` is now set rather than
commented out: 113236 B, 23.0% of the 480K SRAM region. SDRAM is 2334864 B —
3.5% of the 64 MB — which no app type touches.

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

**A bug worth knowing about if you copy `sram-load.cfg` elsewhere**, because
the copies in `../MultiEffect` and `../NewEffectDuino` still have it. The
script catches the bootloader's jump with a hardware breakpoint and
overwrites the image underneath it, and it used to leave VTOR alone on the
grounds that the bootloader had already set it. That is only true when the
image resident in QSPI is itself a `BOOT_SRAM` build. Against a resident
`BOOT_QSPI` image — which is what was on this board — VTOR is left at
`0x90040000`, so every interrupt in the freshly loaded app vectors into the
*other* program's table. The failure is silent and total: the core sits in
the first `HAL_Delay` of `SdramHandle::DeviceInit` forever, because
`SysTick_Handler` is running somewhere else and `uwTick` never moves. This
copy and the one in `../Beads` now set VTOR explicitly instead of inheriting
it.
