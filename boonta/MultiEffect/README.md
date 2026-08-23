# Boonta MultiEffect

Three effects — EQ, drive, reverb — each with six knobs on its own page, each
reorderable in the chain, plus a fourth page for everything global. Built on the
[Template](../Template)'s MVC split, so the same four files do the same four
jobs.

## Layout

| File | Role | Depends on |
|------|------|-----------|
| `PedalState.h/.cpp` | **Model** - what the pedal is set to | nothing |
| `Controls.h/.cpp` | **Controller** - inputs to model, including pickup | libDaisy, model |
| `LedView.h/.cpp` | **View** - model to LEDs and relays | libDaisy, model |
| `Chain.h/.cpp` | **DSP** - runs the three effects in order | model, the three effects |
| `EqEffect.h/.cpp` | three band semi-parametric EQ | model, `Biquad.h` |
| `DriveEffect.h/.cpp` | bias, waveshaper, tone, level | model |
| `ReverbEffect.h/.cpp` | Dattorro plate | model, DaisySP `DelayLine` |
| `Biquad.h` | RBJ second order sections | nothing |
| `MultiEffect.cpp` | wiring only | all of the above |
| `test/` | host-side DSP tests, `make test` | a host compiler |
| `rig/` | hardware measurement rig, `make -C rig verify` | the pedal, an interface, openocd |

The arrows only ever point one way. `Controls` writes the model and never reads
an LED; `LedView` and `Chain` read the model and never write it.

## Pages

The left footswitch cycles the four pages. The six pots mean something different
on each, but every page keeps its own six values, so leaving a page does not
disturb it.

|         | KNOB_1 | KNOB_2 | KNOB_3 | KNOB_4 | KNOB_5 | KNOB_6 |
|---------|--------|--------|--------|--------|--------|--------|
| **EQ** | Low freq | Mid freq | High freq | Low gain | Mid gain | High gain |
| **Drive** | Gain | Tone | Character | Bias | Level | Mix |
| **Reverb** | Time | Damping | Pre-delay | Diffusion | Low cut | Mix |
| **Meta** | EQ amount | Reverb amount | In gain | Drive amount | Out level | Global mix |

- **EQ** is a sweepable low shelf (40 Hz - 500 Hz), peaking mid (200 Hz - 4 kHz)
  and high shelf (1.5 kHz - 12 kHz), each ±15 dB with the centre detent flat.
  Band width is the seventh control there is no knob for, so it lives on a
  toggle and moves all three bands together.
- **Drive** morphs CHARACTER continuously from a soft cubic clip, through a hard
  clip, into a wavefolder. BIAS offsets the signal into the shaper — that is
  what puts even harmonics in — and the DC it leaves is removed afterwards.
- **Reverb** is a Dattorro plate; see below.
- **Meta** amounts are per-slot dry/wet, so an amount at zero takes that effect
  out rather than muting it. IN GAIN is a ±12 dB trim that never mutes; OUT
  LEVEL is unity at the centre and +12 dB at the top.

Per-effect bypass is separate from the amount knob. A short press of the page
footswitch forces that slot fully dry whatever its knob says and leaves the knob
alone, so switching back in restores what was dialled in rather than resetting
it. The long press fires the moment it passes 300 ms rather than waiting for
your foot to lift — a page change you can only feel on release is one you cannot
time — and having fired it latches, so the release does not also toggle a
bypass. A short press on the meta page does nothing; that page edits the chain,
not an effect.

## Controls

| Control | Function |
|---------|----------|
| KNOB_1-6 | The six parameters of the current page |
| TOG_SW_1 | Drive range: down = low gain, centre = mid, up = high |
| TOG_SW_2 | Reverb size: down = room, centre = plate, up = cavern |
| TOG_SW_3 | EQ band width: down = wide, centre = medium, up = tight |
| SW_SEL_1 | Previous chain order |
| SW_SEL_2 | Next chain order |
| SW_FS_1 | **Short press:** switch this page's effect in or out. **Hold 300 ms:** next page (EQ → Drive → Reverb → Meta → EQ) |
| SW_FS_2 | Master bypass — latching, drives the relays |
| EXP | Read into the model; unassigned by default (`kExpressionPage`) |

Two departures from the Template worth knowing about. The left footswitch is
pages, not bypass, so **bypass moved to the right footswitch** — which means the
Template's momentary boost and tap tempo are gone; there is no third footswitch
to put them on. And the toggles are global rather than per page, deliberately: a
switch that changed meaning depending on what was on screen would be a liability
on a dark stage.

The expression input is plumbed but off. Pointing it at the global mix is the
obvious move and the reason it is not the default: `GetExpression()` reads 0
with nothing plugged in, which would pin the pedal fully dry for anyone without
an expression pedal. Set `kExpressionPage` and `kExpressionKnob` in
`Controls.cpp` to enable it.

## Soft pickup

Six pots serve twenty-four parameters, so on arriving at a page a pot almost
never sits where that page left its value. Letting the value snap to the pot
would make every page change an audible lurch, and would mean the pedal did not
really retain anything — so instead each knob is **parked** on arrival and only
starts tracking once the pot sweeps through the stored value.

A knob picks up when the pot lands within 1% of the stored value, *or* when it
crosses it. The crossing test is what makes a fast sweep work: at block rate a
quick turn can step clean over the window without ever landing inside it. There
is a third case — a pot that stops short of its rail can never reach a stored
1.0, and a knob that stays parked however far you turn it reads as a dead pedal,
so the end of travel counts as having got there. If your pots fall well short of
0 or 1, widen `kPickupWindow` in `Controls.cpp`.

The page footswitch LED breathes until you have picked up **one** knob on the
page, then goes solid — the signal being "you have not grabbed anything here
yet", which is exactly the state in which turning a knob appears to do nothing.

It used to breathe until *all six* were picked up. That reads as reasonable and
is useless in practice: you rarely sweep all six knobs on a page, so the LED
pulsed more or less permanently everywhere except the boot page, and a light
that is always on carries no information.

The page showing at boot is the exception: `Controls::Init` arms it immediately,
so at power-on the pots you can see are the truth. Pages you have not visited
hold the defaults in `PedalState.cpp` until you take them over.

## Chain order

The two select buttons step through the six permutations of three effects, in
either direction, from any page — so the worst case from any order to any other
is three presses. The three select LEDs show the result: **one LED per position
in the chain, coloured by the effect sitting there**. Those are the same colours
the page footswitch shows, so each effect's colour is learned once.

| | Colour | Channels |
|---|---|---|
| EQ | teal | green + blue |
| Drive | gold | red + green |
| Reverb | purple | red + blue |
| Meta page | white | all three |

Each effect is one of the three secondaries, which keeps them maximally distinct
from one another and from the green and red the bypass footswitch uses. A slot
that has been switched out keeps its colour but drops to 20% brightness, so the
chain order stays readable while the state of each effect is obvious.

One trap, since it cost an investigation here. `DaisyBoonta::SetSelectLed()`
appears to have its green and blue crossed — it writes the caller's green to the
channel named `_B` and blue to `_G` — and to permute the indices, mapping
`SELECT_LED_1` onto the channels named `SELECT_2`. Both are deliberate: the
`_G`/`_B` names in that enum are reversed with respect to the wiring, and the
physical row runs 2/1/3, so the setter's swaps are the correction. Confirmed on
hardware — these colours light the row teal, gold, purple from the left.
Straightening out the setter would silently exchange drive and reverb, and would
not touch the footswitch LEDs, which go through a path with no swap.

The bypass footswitch is **green in circuit, red out** — readable from standing
height in a way a brightness difference on one colour is not.

## Reverb

`DaisySP-LGPL` is not checked out in this tree, so `ReverbSc` is unavailable —
and pulling it in would put an LGPL component in a pedal binary. It also exposes
only feedback and damping, where the reverb page promises pre-delay and
diffusion too. Both fall out of a plate topology for free, so the plate is
written out here: pre-delay, low cut, bandwidth limit, four input diffusers,
then a figure-of-eight tank of two halves, cross-fed, two of its allpasses
slowly modulated so it does not ring on a fixed set of modes. Stereo comes from
seven taps per side into those lines rather than from two reverbs.

Half a megabyte of delay memory, in SDRAM — which is not cleared at startup, so
every line is explicitly reset in `Init()`.

Measured decay, fully wet, damping off, driven with a sustained two-note chord
at 0.7 then left in silence:

| Size | TIME min | TIME centre | TIME max | Peak build-up at TIME max |
|------|---------|-------------|----------|---------------------------|
| Room | 0.7 s | 1.2 s | 2.9 s | 0.85× |
| Plate | 1.4 s | 2.6 s | 5.1 s | 1.13× |
| Cavern | 2.2 s | 3.5 s | 10.2 s | 1.10× |

Two numbers in `ReverbEffect.cpp` came out of that measurement rather than out
of the paper, and both are worth leaving alone:

- **The decay ceiling is one value for all three sizes.** Reverb time is loop
  length over loop loss, and the size toggle already multiplies loop length by
  0.5, 1 and 1.6 — so holding the coefficient still is what makes a cavern ring
  longer than a room. Raising it with size compounds the two: a ceiling of 0.94
  on the cavern left a tail still audible after sixteen seconds and built up to
  nearly three times the input, loud enough to clip the codec.
- **Each size has a wet trim.** The output taps sit at fixed positions in a tank
  whose length moves under them, so how much of their sum cancels changes with
  it. Untrimmed, plate to cavern jumped the wet level 9 dB, which reads as a
  volume control rather than a size control. Trimmed, the three sit within about
  2 dB across the whole TIME range.

## Execution contexts

Two contexts, and the split matters:

- **Audio callback** (`AudioCallback` in `MultiEffect.cpp`) runs
  `Controls::Process()` then `Chain::Process()`. `DaisyBoonta` initialises its
  `AnalogControl` smoothing filters with `AudioCallbackRate()`, so controls
  *must* be read once per callback — and the block size must be left where
  `Init()` put it, or every knob on the pedal is detuned. `Chain` chunks
  internally at 128 frames and copes with any block size.
- **Main loop** runs `LedView::Update()`. The LED driver transfer is I2C DMA and
  the relays are mechanical, so neither belongs in the callback. `LedView` only
  writes the relays when bypass actually changes, so they don't chatter.

Only the master bypass moves the relays. Per-effect bypass is a crossfade inside
the DSP and never touches one — clicking a relay per effect would be audible and
hard on the contacts. That does mean switching all three effects out is not the
same as bypassing the pedal: you are still going through the codec, the input
trim and the output level.

**All three relays follow the bypass state together**, which is what
`HardwareTest` does and the opposite of the Template:

| | input | output | bypass |
|---|---|---|---|
| Active | 1 | 1 | 1 |
| Bypassed | 0 | 0 | 0 |

The Template drives the bypass relay *inverted* — energised when the pedal is
out of circuit — and that is backwards for this board. De-energised, the bypass
relay **closes** an analog path straight from input jack to output jack. So with
the Template's wiring the dry signal sums with the DSP output whenever the
effect is supposedly engaged, partially cancelling it, and the pedal goes silent
when it is supposedly bypassed.

Measured with a guitar in the pedal and its output captured, interleaved so
playing dynamics cancel:

| relays (in/out/byp) | codec in | pedal out |
|---|---|---|
| 1 / 1 / 1 | −28.5 dB | **−15.2 dB** |
| 1 / 1 / 0 (Template) | −30.5 dB | −26.0 dB |

and end to end, driving only the model:

| | codec in | pedal out |
|---|---|---|
| Active | −24.7 dB | −7.7 dB |
| Bypassed | −54.9 dB | −23.1 dB |

Bypassed, the codec is cut off but the output still carries dry signal through
that analog path — true bypass, as intended.

The model is written from the callback and read from the main loop. Every member
is a naturally aligned word-sized POD written by exactly one context, so the
worst case for the reader is one frame of stale LED colour.

Every slot runs even at zero amount. Skipping would save cycles the H7 does not
need, and would freeze the reverb tank mid-tail, so turning a slot back up would
resume a stale tail rather than a decayed one.

## Build

```
make
make program-dfu
```

`make test` runs the host-side checks; see [Tests](#tests).

`make program` needs openocd's script directory, and its default is a Unix path.
On a Windows toolchain the Makefile's unquoted `-s $(OCD_DIR)` also breaks on the
space in `Program Files`, so invoke openocd directly instead:

```bash
openocd -s "/c/Program Files/DaisyToolchain/openocd/scripts" -f interface/stlink.cfg -f target/stm32h7x.cfg -c "program ./build/MultiEffect.elf verify reset exit"
```

Everything fits in the 128K internal flash, so unlike a lot of three-effect
builds this one needs no bootloader — the reverb tank is large but it lives in
SDRAM, which no app type touches. Verified:

| `APP_TYPE` | Code lands in | Used | SDRAM |
|---|---|---|---|
| `BOOT_NONE` (default) | internal FLASH, 128K | 101092 B, 77% | 540828 B |
| `BOOT_SRAM` | SRAM, 480K | 100840 B, 21% | 540828 B |
| `BOOT_QSPI` | QSPI, 7936K | 100840 B, 1% | 540828 B |

That leaves about 27K of flash. If you outgrow it, uncomment `APP_TYPE` in the
`Makefile` and run `make program-boot` once; `make program` (openocd) and the
`.vscode` debug configuration go away when you do.

## Tests

`PedalState`, `Chain` and the three effects are free of libDaisy, so they build
and run on your desktop — no board, no flashing:

```bash
make test
```

That delegates to [`test/`](test), which also holds the reverb measurement that
produced the table above:

```bash
make -C test decay
```

Any host compiler will do; `test/Makefile` defaults to `clang++` and takes
`make CXX=... ` to override. It builds the real `.cpp` files out of the parent
directory, not copies, against two stubs in `test/stubs` standing in for the one
DaisySP header and the one libDaisy macro the reverb needs.

Seventeen checks: flat EQ passes at unity, the shelves reach their rated gain
without disturbing distant bands, drive stays bounded across the whole CHARACTER
sweep at full gain, full BIAS leaves no DC, the reverb charges and decays to
silence at every size, all six chain orders contain each effect once and
reordering audibly changes the result, a slot at zero amount is transparent,
bypass passes the input bit for bit, and a 1000-frame block matches the same
audio in 48-frame blocks.

### On hardware

Flashed to a Boonta over ST-Link and inspected with gdb while running. Confirmed
there: the audio callback runs at 48 kHz with a block of 48; the ADC reaches the
model, and the toggles with it; the main loop drives `LedView`, which drove the
relays when bypass changed; and soft pickup parks exactly the knobs it should.
That last one is worth spelling out, because it is the piece the host tests
cannot reach. With the pots sitting at `0.632 / 0 / 1 / 1 / 1 / 1` and the drive
page holding its defaults of `0.35 / 0.5 / 0.25 / 0.5 / 0.5 / 1.0`, switching to
that page parked five knobs and picked up the sixth — the one whose pot already
matched — giving `pickup_pending == 0x1f`, and the drive page kept its own values
while the EQ page kept its.

**CPU load, measured: 33.8% average, 40.7% peak**, whole chain active, all three
effects running, 48 kHz. That is about ten times the "few percent" this README
previously estimated, so treat the estimate as retracted. It runs with headroom
but not a lot of it, and the reverb's SDRAM traffic is the obvious suspect —
about forty external-memory accesses per sample, which are far more expensive
than the cycle count suggests. Note the pedal boots bypassed, where `Chain`
early-outs at 0.4%; you have to take it out of bypass to see the real figure.

To measure it yourself, uncomment the `C_DEFS += -DPROFILE_CPU` line in the
`Makefile`, then with the board running:

```bash
arm-none-eabi-gdb build/MultiEffect.elf -batch -ex 'target extended-remote :3333' -ex 'monitor halt' -ex 'print profile' -ex 'monitor resume' -ex 'detach'
```

### On the bench

[`rig/`](rig) measures the assembled pedal — what reaches the codec, what leaves
the output jack, and whether the DSP does what it claims. It needs a source into
the pedal (a guitar is fine), the pedal's output patched to an interface input,
openocd, and a `PROBE_IO` build. `make -C rig verify` is the regression check
that catches the relay polarity; `make -C rig dsp` A/Bs the effects.

Measured there, interleaved so playing dynamics cancel:

- **Relays** — engaging the pedal puts the codec 23–30 dB above the bypassed
  case, and bypassed still passes dry through the analog path.
- **EQ** — with the mid band tuned onto a sustained 262 Hz note and the other
  effects switched out, −14.3 dB at full cut and +12.8 dB at full boost against
  flat: a 27.1 dB sweep against a rated 30.
- **Drive** — 12.3 dB of added harmonic energy across 1.5–8 kHz when switched in.

Measuring the EQ needs care about *where* the stimulus has energy, and about the
reverb tail crossing between A/B captures; both traps are written up in the
rig's own README.

### Still not covered

Nobody has listened to it. Every DSP claim above is a measurement, not a
judgement about whether it sounds good — the EQ shelves move the right bands by
the right amounts, but nothing says the reverb is pleasant or the drive is
musical. The select buttons and LED colours have been checked by eye; the
footswitches by hand; everything else by writing the model from a debugger.
