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
| `SavedState.h` | what survives a power cycle, and the conversion either way | model |
| `MidiMap.h` | which controller does what | model |
| `MidiControl.h/.cpp` | **Controller, remote** - MIDI in to model | libDaisy, model |
| `Storage.h/.cpp` | that block in QSPI flash, debounced | libDaisy, model |
| `UsbDiag.h/.cpp` | USB clock trim, PHY ungate, and a register snapshot for the ST-Link | libDaisy |
| `sram-load.cfg` | openocd script behind `make sram`: load into SRAM over the ST-Link | openocd, the bootloader |
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

The page footswitch LED breathes while **nothing on this page has been changed
since you arrived at it**, and goes solid the moment something has. That is also
the state in which turning a knob appears to do nothing, because every knob is
still parked.

Two earlier conditions were tried and are worth knowing about, because both look
reasonable written down:

- *"any knob still parked"* is true on almost every page almost always — you
  rarely sweep all six — so the LED pulsed permanently and said nothing.
- *"any knob picked up"* clears the instant a pot happens to sit on its stored
  value, which can happen on the very first block after arriving, with nothing
  edited. On the bench that turned out to be common rather than a corner case.

So the test compares each knob against the value it held **on arrival**, not
against the previous block — an armed knob is rewritten from its pot every
block, so a frame-to-frame comparison reads as zero however far you turn it.

At boot the knobs are parked, exactly as on any other page arrival, so the pedal
comes up on its saved settings rather than adopting whatever the pots read. See
below.

## Saved settings

Every parameter on all four pages, the current page, the chain order, the three
per-effect bypasses and the master bypass are kept in the Seed's QSPI flash and
restored at power-on. The pedal comes back up sounding as you left it.

Not saved: the expression reading, which knobs are parked, and whether the page
has been edited — all rebuilt at boot from the hardware. Nor the toggles, which
cannot usefully be saved: they are physical three-position switches, so their
position at power-on *is* the truth.

**This changes what the knobs do at boot.** The pots no longer win at power-on;
the saved values do, and a knob takes over when you sweep it through its stored
value, exactly as when changing page. Without that, restoring settings would be
pointless — the visible page would be overwritten from the pots on the first
audio block.

Knowing *when* to write is the whole problem. A knob being turned changes the
model on every audio block, and each save is a flash erase plus a write: slow,
blocking, and finite at roughly 100k erase cycles. So the save is debounced —
nothing is written until the pedal has been left alone for two seconds. A full
knob sweep costs one erase, not a thousand. It runs from the main loop, never
the audio callback.

Two details that are easy to get wrong, and both were:

- **The comparison needs a tolerance, not equality.** A picked-up knob is
  rewritten from its pot every block and the smoothed ADC value wanders in the
  last few decimals forever. Compared exactly, a pedal sitting untouched looks
  like it is being changed a thousand times a second: the settle timer never
  expires, so nothing is ever saved. `SavedState::kEpsilon` is two parts in a
  thousand — far above that noise, far below anything audible.
- **"Differs from what is saved" and "has stopped moving" are different
  questions.** The first is asked against the last block written, the second
  against the last block seen. Conflating them reproduces the bug above even
  with a tolerance in place.

`SavedState::kVersion` guards the layout: a block written by a different version
is refused and the defaults stand, which is the difference between "my settings
reset" and a pedal booting with garbage in its parameters. `ApplyState` also
range-checks every value, NaN included, because flash that has never been
written reads as whatever was left in it.

## MIDI

Every parameter and every switch is addressable, on USB and the DIN/TRS input at
once - whichever sends a message wins, and it does not matter which one it came
in on. Omni by default; set `midimap::kChannel` to listen to one channel.

| CC | Controls |
|----|----------|
| 20 - 25 | EQ page, knobs 1-6 |
| 26 - 31 | Drive page, knobs 1-6 |
| 102 - 107 | Reverb page, knobs 1-6 |
| 108 - 113 | Meta page, knobs 1-6 |
| 114 | Master bypass (>=64 in circuit) |
| 115 - 117 | EQ / drive / reverb switched in (>=64 in) |
| 118 | Chain order, scaled across the six |
| 119 | Page select, scaled across the four |

Verified end to end over the TRS input: all four pages of parameters, master
bypass and the per-effect bypasses both ways, chain order across its range, page
select, program-change recall of presets 5 and 0, and unmapped controllers
correctly ignored.

**USB works. Windows was binding the wrong driver to it.** The
symptom was that the Daisy never appeared as a MIDI device while Device Manager
showed "Unknown USB Device (Device Descriptor Request Failed)". Every part of
that reading was wrong, and it cost a long detour into libDaisy's USB stack
before anyone looked at the host.

What is actually true, read off the running target and Windows' own PnP store:

- `DCFG.DAD` is **42**. The device has been assigned an address, which cannot
  happen unless it answered `GET_DESCRIPTOR(DEVICE)` first.
- Windows has the device's strings: `BusReportedDeviceDesc` reads
  **"Daisy Seed Built In"**, which is libDaisy's `USBD_PRODUCT_STRING_FS`.
- Its compatible IDs include `USB\Class_01&SubClass_01&Prot_00` -- USB Audio,
  Audio Control -- so the MIDI descriptor set is being emitted and read.
- The failing "Device Descriptor Request Failed" node is **a different device**:
  `VID_0000&PID_0002` on `Port_#0012.Hub_#0005`, while the Daisy sits on
  `Port_#0014.Hub_#0004`. Unrelated, and it was never the Daisy.

The device's status is `CM_PROB_FAILED_START` (Code 10) with `Service = usbser`
and `DriverDesc = "crow: telephone line"`. The cause is a driver package in the
store:

```
Published Name:  oem96.inf
Original Name:   crow_telephone_line.inf
Provider Name:   libwdi
Class Name:      Ports
Signer Name:     USB\VID_0483&PID_5740 (libwdi autogenerated)
Attributes:      Legacy
```

A Zadig/libwdi-generated driver, installed for a Monome Crow, which hard-binds
`USB\VID_0483&PID_5740` -- and that is exactly the VID/PID libDaisy ships
(`USBD_VID 1155`, `USBD_PID_FS 22336`, the generic STMicro CDC pair half the
STM32 world uses). A hardware-ID match outranks the device's own compatible IDs,
so Windows loads `usbser` on a device that is not a serial port, and `usbser`
cannot start. The MIDI function never gets as far as `usbaudio.sys`.

The DFU observation that seemed to exonerate the host was the clue all along:
the bootloader has a libwdi package of its own (`daisy_bootloader.inf`, same
board serial `3975395A3333`). Both ends of this board had been Zadig'd.

**Fixed by changing the PID.** `USBD_PID_FS` in libDaisy's `usbd_desc.c` is now
`55829` (`0xDA15`), and `USBD_PID_HS` is `55830` (`0xDA16`) -- they had shared
`22336` despite carrying different product strings. Nothing depends on `0x5740`
for MIDI, and moving off it sidesteps every host with a stale CDC binding cached
for that pair, which given how many projects ship it is the right default rather
than a workaround.

Note that changing a header in libDaisy is not enough on its own: the descriptor
lives in `libdaisy.a`, and the app's link step does not depend on the archive, so
the app happily relinks the old PID. Rebuild libDaisy *and* clean-build the app.

Verified on the bench after reflashing:

```
USB\VID_0483&PID_DA15\3975395A3333
  Status  OK          (was Error / CM_PROB_FAILED_START)
  Class   MEDIA       (was Ports)
  Service usbaudio    (was usbser)

winmm MIDI output ports:  [4] Daisy Seed Built In
winmm MIDI input  ports:  [3] Daisy Seed Built In
```

And end to end, sending `CC 118 = 117` to that port from the host:

```
MidiControl::ReceivedUsb()   0 -> 1
MidiControl::ReceivedUart()  0 -> 0     (it really did arrive over USB)
MidiControl::Accepted()      0 -> 1
state.order_                 0 -> 5     Reverb -> Drive -> EQ, as asked
```

The host-side alternative -- `pnputil /delete-driver oem96.inf /uninstall /force`
as administrator, then replug -- also works, but it takes the driver away from
the Crow, and it fixes one machine rather than the firmware.

Windows exposes a MIDI **input** port as well as an output, so the descriptor
already declares both directions. A SysEx dump would be purely a `MidiControl`
change; there is no descriptor work waiting behind it.

### What was ruled out on the way

Both candidates below were investigated with the board on the bench. Neither is
the cause; the notes stay because one is a live libDaisy bug and the other is a
measurement worth not repeating.

**HSI48 accuracy: eliminated.** libDaisy points the USB clock at HSI48 and never
enables the Clock Recovery System -- `HAL_RCCEx_CRSConfig` appears nowhere in
the tree -- while per **AN2606** the ROM DFU bootloader does enable CRS. That
made a free-running oscillator the leading suspect. `usbdiag::EnableCrs()` sets
it up properly (`SYNCSRC` = USB2 -- on the H750 the core libDaisy calls
`USB_OTG_FS` is *USB2* at `0x40080000`; syncing to USB1 compiles and never
locks). Measured: `CRS_ISR.SYNCOKF` sets, so sync is genuinely arriving, and
`CRS_CR.TRIM` never moves off its default 32 with `AUTOTRIMEN` set. With the bus
live, `CRS_ISR.FECAP` reads **9** -- nine cycles in 48000, or 0.019%, against a
USB budget of 0.25% and a `FELIM` of 34. **HSI48 was inside spec by an order of
magnitude.** The oscillator was never the problem, and CRS is now locked to the
host anyway, which costs nothing and is what ST's own bootloader does.

**PHY clock gating: real, but not this.** `usbd_conf.c`'s suspend callback calls
`__HAL_PCD_GATE_PHYCLOCK()` unconditionally and nothing ungates it -- upstream
as [libDaisy#716](https://github.com/electro-smith/libDaisy/issues/716), still
open, and the gating is not even wanted here since `low_power_enable` is
`DISABLE`. Confirmed firing on this board: `usbdiag::snapshot.ungated`
increments by two on every attach. Worth keeping fixed. It does not affect
enumeration.

**A trap worth writing down.** Halting the core to read USB registers fails any
enumeration in progress -- the host times out and Windows reports a descriptor
failure whatever the firmware is doing. Some of the original register-level
evidence was gathered that way, which is part of why the wrong conclusion held
for so long. Read while running, or read after the fact.

UART MIDI is unaffected and does everything USB would have.

Every number is in a range the MIDI specification leaves undefined - 20-31 and
102-119. Notably *not* 32-63: those are the LSBs of controllers 0-31, and
although most gear ignores them, a controller sending 14-bit CCs would move two
parameters at once. Anything unmapped is ignored, which is most of the 128 - a
pedal that lurched every time something sent modulation would be unusable.

[**boonta-multieffect-editor**](https://github.com/taubaland/boonta-multieffect-editor) is a browser front end for all of the
above: every parameter on screen, the sixteen slots as a librarian with names
and JSON export, and the DSP ported to an AudioWorklet so a sound can be dialled
in before it is sent. It sends and cannot receive, for the reason above -- there
is no MIDI out here.

It lives in its own repository so it can be hosted as a static page. That makes
one thing this repository's problem: its `js/midi.js` is a transcription of
`MidiMap.h` and its `js/params.js` of the ranges in the effect sources. **Change
a controller number or a sweep range here and it has to change there too.**
Nothing detects the drift -- the editor would simply send the wrong numbers, and
a pedal with no MIDI out cannot contradict it.

**A hand on the pedal beats a controller across the room.** Nothing parks the pot
when a CC arrives, so a remote change to a parameter whose knob is currently
picked up is overwritten on the next audio block. Leave the knob alone and the
remote value stands. That falls out of soft pickup rather than being special-
cased, and it is the right way round.

## Presets

Sixteen, recalled with **program change 0-15** - which is what program change is
for, rather than spending a controller on it.

There is no separate save gesture. The live settings are written back into
whichever preset is current, so a preset is a working slot rather than a snapshot
you have to remember to commit; on a pedal with no screen, a save step you can
forget is a save step that loses your sound. Recalling another preset saves the
one you are leaving first, including edits still inside the settle window.

Recalling replaces every parameter at once, so the knobs are re-parked against
the new values - otherwise a picked-up knob would overwrite what was just
loaded on the next block.

The whole bank is one block in flash: `PersistentStorage` keeps a single struct
at a single address, and the flash erases a sector at a time, so writing one
preset costs exactly what writing all sixteen costs.

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
make sram
```

`make test` runs the host-side checks; see [Tests](#tests).

This project runs from **SRAM under the Daisy bootloader** (`APP_TYPE =
BOOT_SRAM`). It used to run from internal flash and no longer fits comfortably:
with MIDI, presets and `UsbDiag` the build reached 91.9% of the 128K, and one
upstream libDaisy merge cost 380 bytes of the ~8K left. The same build is 24.5%
of the 480K SRAM.

| `APP_TYPE` | Code lands in | Used |
|---|---|---|
| `BOOT_NONE` | internal FLASH, 128K | 120436 B, 91.9% |
| `BOOT_SRAM` (this project) | SRAM, 480K | 120444 B, 24.5% |
| `BOOT_QSPI` | QSPI, 7936K | ~120K, 1% |

The reverb tank is another 540828 B, but it lives in SDRAM, which no app type
touches.

### Three ways to get code onto the board

| target | writes | needs | survives power cycle |
|---|---|---|---|
| `make boot` | bootloader → internal flash | ST-Link | yes, it *is* the boot path |
| `make program-dfu` | app → QSPI at `0x90040000` | the bootloader in DFU | yes |
| `make sram` | app → SRAM, ~1 s | ST-Link, bootloader, an app already in QSPI | no |

`make boot` is worth knowing about: libDaisy's `program-boot` needs the chip
*already* in DFU mode, which is a chicken-and-egg problem on a board whose app
has stopped booting. openocd writes internal flash with no such requirement, so
the bootloader can always be reinstalled — and so can a `BOOT_NONE` app, if you
want the pedal back the way it was.

To get the bootloader into DFU when there is no working app to ask, write
`0xB0074EFA` (`BootInfo::Type::INF_TIMEOUT`) to `boot_info.status` in backup
SRAM and reset — it then waits in DFU indefinitely instead of timing out after
2 s:

```bash
openocd -s "/c/Program Files/DaisyToolchain/openocd/scripts" -f interface/stlink.cfg -f target/stm32h7x.cfg -c "init; halt; mmw 0x580244e0 0x10000000 0; mmw 0x58024800 0x00000100 0; mww 0x38800000 0xB0074EFA; reset run; shutdown"
```

(The two `mmw` writes enable the backup SRAM clock and clear the backup-domain
write protect, which `System::InitBackupSram()` normally does.)

### The SRAM loop, and why it is shaped the way it is

`make sram` takes about a second, writes no flash, and wears nothing out. Its
one subtlety is *where* it takes control, and getting that wrong is a long
debugging session.

A `BOOT_SRAM` image is **not self-contained**. `startup_stm32h750xx.c` skips
`SystemInit()` when `BOOT_APP` is defined, and `DaisySeed::Init` only configures
the clocks at all when it can see a bootloader — it reads `boot_info.version`
out of backup SRAM, and sets `skip_clocks = true` if that says `LT_v6_0`, which
is what uninitialised memory reads as. Load such an image with no bootloader
present and the clocks are never configured: HSI48 stays off (`RCC->CR` reads
`0x0000c025`, HSI only), so the OTG core's soft reset can never complete
(`GRSTCTL` sticks at `0x80000001` — AHB idle, `CSRST` asserted forever),
`USB_CoreInit` returns `HAL_ERROR`, and libDaisy's `Error_Handler()` is a bare
`while(1)` with no output. The symptom is a board that looks like it is running
and does nothing.

Nor can you let the bootloader run and then force PC/SP yourself. The
bootloader spends its time in USB interrupts, so a halt usually lands in Handler
mode; the app's first stack pop then returns from an exception that is still
active and bus-faults off the top of DTCMRAM (`BFAR = 0x20020014`, `CFSR`
BusFault escalated to a forced HardFault).

So `sram-load.cfg` lets the bootloader do the entire handover and catches the
app on its first instruction with a hardware breakpoint. At that point the core
is in Thread mode, `MSP` is `0x20020000`, `VTOR` is `0x24000000` and the clocks
are up — all of it set by the bootloader, none of it faked. Then the image is
overwritten, `PC` put back, and the core resumed. Measured: caught at
`0x24000a08` in Thread mode, `verify_image` confirms the resident SRAM matches
the ELF just built and no longer matches the one in QSPI.

Two details that cost time and are easy to miss:

- The ELF entry point has the Thumb bit set — `readelf` reports `0x24000a09`
  for an entry at `0x24000a08`. Cortex-M refuses an odd `PC` and openocd says
  only "Error setting register pc", which is easy to read past because the
  breakpoint has usually left `PC` in the right place anyway.
- The breakpoint has to be on the **resident** image's entry, not the new one,
  since it is the old image the bootloader is about to jump into. The script
  reads it from the reset vector at `0x24000004` and falls back to the new
  ELF's entry on a cold start.

### QSPI over the ST-Link

Tempting, and openocd nearly supports it: the shipped `target/stm32h7x.cfg`
declares an `stmqspi` bank at `0x90000000`, enabled with `-c "set QUADSPI 1"`.
It did not work here. `stmqspi` drives the QUADSPI peripheral registers
directly, and the running app owns that peripheral too through `Storage` —
`flash probe` wedged the core hard enough that every reconnect failed
examination. Reconnecting at `adapter speed 480` recovered it without a power
cycle, but the pedal lost the chain order stored in its current preset, so
something got written that should not have been. Doing it properly needs the app
stopped *and* a QSPI init sequence openocd can run itself, which is what the
bootloader otherwise provides. `make program-dfu` is the supported path.

The bootloader advertises the QSPI map, which is worth recording:

```
@Flash /0x90000000/64*4Kg/0x90040000/60*64Kg/0x90400000/60*64Kg
```

The preset bank is at offset 0 — `PersistentStorage::Init` defaults to
`address_offset = 0` — so it sits in the first 256K, and the app goes in at
`0x90040000`. They do not collide, and a `program-dfu` leaves presets intact.
Confirmed: the stored chain order survived writing the app.

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
