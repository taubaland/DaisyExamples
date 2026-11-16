# Boonta Clouds Secrets

Boonta Clouds with its hidden layer opened: the alternate modes Mutable
Instruments kept behind a five-second button hold, including the spectral one
that the plain `../Clouds` build leaves out. It is `../Clouds` plus one new DSP
class (`Spectral`), a long-hold gesture, and a fourth mode; everything else is
shared in spirit and described in that README. Below is what differs.

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
| SW_SEL_1 / SW_SEL_2 | Previous / next hidden mode. **Hold SW_SEL_2 for five seconds** to open or close the hidden layer |
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

### The hidden layer

The pedal powers up in granular, with only the first select LED lit, and the
select buttons do nothing: there is nothing yet to choose between. Hold
**SW_SEL_2 for five seconds** and all three select LEDs flash orange. The
hidden layer is open, the pedal is in stretch, and the select buttons now step
through the three hidden modes. Hold SW_SEL_2 for another five seconds, the
LEDs flash again, and the pedal is back in granular with the layer closed.

The buttons act on release, so the press that begins a long hold does not
step the mode first. MIDI is not gated: CC 29 and Program Change reach every
mode directly, and choosing a hidden one opens the layer.

| Mode | LED (layer open) | Behaviour |
|------|------------------|-----------|
| **Granular** | violet, layer closed | Random start points, random spacing, random panning. The cloud. |
| **Stretch** | 1st, teal | One playhead with two grains crossfading over it. DENSITY becomes playback rate: stopped at the bottom, real time at the centre, double speed at the top. |
| **Delay** | 2nd, amber | The same crossfade with the playhead locked to real time, so at unity PITCH it is a plain delay and off unity a transposing one. POSITION is the delay time; DENSITY damps the feedback path. Freeze turns it into a looper. |
| **Spectral** | 3rd, crimson | A phase vocoder, misused. See below. |

The selected mode's LED doubles as an output meter.

### Spectral mode

The input is cut into overlapping 1024-sample frames, each is windowed and
transformed, and per bin the engine keeps a magnitude and an *instantaneous
frequency*. Output is made by running a phase accumulator at each bin's
frequency, inverse transforming and overlap-adding. Keeping frequency rather
than phase is what lets a stored frame be played back indefinitely as a
sustained tone rather than a buzz.

Every knob is neutral at centre. With all of them at centre the mode is
transparent: a steady tone comes out at the same level and pitch, delayed by
about 32 ms. (PITCH is neutral at centre as it is everywhere.)

| Knob | In Spectral mode |
|------|------------------|
| POSITION | Which of six snapshot slots. Unfrozen: the slot being written. Frozen: a smooth scan between the stored slots. |
| SIZE | Left of centre, the whole spectrum slides upward (up to half the range). Right of centre it is blended with its own mirror image, ending in full reversal. |
| PITCH | Spectral transposition by resampling the bins. |
| DENSITY | Left of centre, a growing share of bins stop updating: partial freeze. Right of centre, frames are averaged, which smooths every bin's level; past 90% random phase is added until the sound becomes a wash. |
| TEXTURE | Left of centre, bin levels are quantised, like a low bit-rate codec. Right of centre the spectrum is flattened, the loudest partials coming down and the quietest coming up, at constant overall level. |
| MIX | Dry/wet. The dry path is immediate; the wet path is about 32 ms late. |
| FEEDBACK, REVERB | As elsewhere. |
| SPREAD | No effect: the stereo image is carried through untouched. |
| FREEZE | Stops capturing frames and plays the stored slots. |

Not carried over from the original: it has a TRIG input that fires spectral
glitches, and the Boonta has no trigger jack to put one on.

Spectral work is spread over five stages per frame, one per audio block, so
no single block carries a whole frame's transforms. See `Spectral.h`.

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
| 29 | Playback mode, scaled across the four: granular, stretch, delay, spectral |
| 30 | Freeze (≥64 frozen) |
| 31 | Bypass (≥64 in circuit) |

Program Change 0–3 also selects the playback mode. The numbers live in 20–31,
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
- **It takes the parameter back when it is turned to within 10% of it.**
  A displaced knob does nothing until it has moved at least 2% of travel
  *and* is within 10% of travel of the value the parameter now holds. Then it
  picks the parameter up and tracks it continuously, so the sound never
  jumps. The 2% is what stops a knob parked near a MIDI value taking it
  straight back.

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
by `kMaxGrains` in `Effect.h` — 24 — and by the reverb. On the plain
`../Clouds` build that was measured on the board and has headroom everywhere
except one corner. Those figures are for the grain modes and carry over; the
spectral mode's cost is **not measured**, see Status below.

## Files

| File | Role |
|------|------|
| `Persist.h/.cpp` | Controller — saves and restores the model in QSPI flash |
| `PedalState.h/.cpp` | Model — params, freeze, mode and the hidden-layer state, output telemetry |
| `Controls.h/.cpp` | Controller — knob table, toggle table, freeze machine, the pickup rule |
| `MidiControl.h/.cpp` | Controller — USB and DIN MIDI into the same model |
| `MidiMap.h` | Which CC does what |
| `LedView.h/.cpp` | View — freeze LED, bypass LED, mode LEDs and meter, relays |
| `Effect.h/.cpp` | The buffer, the grain pool and the three schedulers; hands spectral mode to `Spectral` |
| `Spectral.h/.cpp` | The phase vocoder behind the fourth mode |
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

## Saved state

`Persist.h/.cpp` keeps the model across a power cycle. It watches
`PedalState::Capture()`; when that has stood unchanged for two seconds it
writes it to one 4 KB sector of the QSPI flash, seven megabytes in, using
libDaisy's `PersistentStorage`. No two writes are closer than ten seconds,
because flash wears per erase. The write blocks for tens of milliseconds, so
it happens from the main loop and never the audio callback; the callback
keeps running because under BOOT_SRAM nothing it executes comes from the flash.

Saved: knobs, toggle positions and the playback mode, including a hidden one. Not saved: bypass, which always
boots out of circuit, and freeze.

At boot `PedalState::Restore()` validates what it reads (magic number, every
value a number in 0 to 1, every position one that exists) and refuses the lot
if anything is off, so erased, half-written or another firmware's flash starts
the pedal from defaults. Each firmware has its own magic number.

A restored model is handed to `Controls::Init(..., restored = true)`, which
parks every knob exactly as if MIDI had written it: the knob takes its
parameter back by being turned within 10% of the saved value. Toggles keep
their saved position until flipped.

Host tests cover capture, restore, and refusal of bad data. The flash write
itself needs the board and **has not been run on one**.

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

## Status

What has been done:

- Builds for the pedal (`make`), under the bootloader like Clouds and Beads.
- The host tests pass (`make test`): everything Clouds checks, plus the hidden
  layer's state machine and, for Spectral, that a neutral setting is
  transparent in level and pitch, that PITCH +1 octave doubles the frequency,
  that freeze sustains after the input stops while unfrozen silence decays to
  zero, that bypass is bit exact, that blocks bigger than a hop still keep
  up, and that no knob position, alone or at any corner, produces NaN or a
  runaway level, with feedback on and noise in.

What has **not** been done: none of it has run on a pedal. The five-second
hold, the LED behaviour, and above all the CPU cost of Spectral are unverified
on hardware. On a host, spectral mode costs more per block than dense
granular does, so measure it with the `PROFILE_CPU` counters (left on in the
Makefile) before trusting it on stage; the README of `../Clouds` has the gdb
one-liner. The DTCM region is 89% full, which leaves the stack about 14 KB.
