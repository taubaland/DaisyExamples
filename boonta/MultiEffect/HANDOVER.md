# Handover

State of the Boonta MultiEffect as of this session's end, for whoever picks it
up next. Delete this file once it has been absorbed — the durable material is
already in [`README.md`](README.md) and [`rig/README.md`](rig/README.md); this
is only what a new session cannot infer from the tree.

## Where things stand

Branch **`boonta-midi-presets`**, pushed. Three commits ahead of the last merge:

```
57f6c36  Make the effect slots pluggable, and adapt exported gen~/RNBO patches
2336447  Add a feedback-loop check to the measurement rig
08650d2  Run from SRAM under the bootloader, and load it over the ST-Link
```

**Uncommitted, and deliberately so** — the RNBO pitch shifter integration:

```
 M EffectRegistry.cpp / EffectRegistry.h   register the patch as effect id 3
 M Makefile                                RNBO_DIR / RNBO_PATCH build path
?? RnboEffect.cpp / RnboEffect.h           the adapter
?? RnboPatch.h                             include shim for the exported .cpp
```

It builds and links. It has **never been run**, and its CPU cost is unmeasured.
That is the first thing to do.

## Build and load

```bash
make                      # default three effects
make sram                 # load into SRAM over the ST-Link, via the bootloader
make -C test run          # 60 host checks, no hardware
```

With the RNBO patch:

```bash
make RNBO_DIR=/d/Projects/test RNBO_PATCH=rnbo.pitchshifter.cpp
```

`APP_TYPE = BOOT_SRAM`, so `make program` (openocd straight to flash) is refused
by libDaisy — `make sram` is the path, and it works by catching the bootloader's
handover with a hardware breakpoint. See `sram-load.cfg` for why it cannot just
force PC and SP.

openocd, for anything the Makefile does not wrap:

```bash
openocd -s "/c/Program Files/DaisyToolchain/openocd/scripts" -f interface/stlink.cfg -f target/stm32h7x.cfg
```

`OCD_DIR` defaults to a Unix path and the Makefile does not quote it, so a
Windows path with a space in it breaks `make program`. Invoke openocd directly.

## Do this first

1. **Measure the RNBO patch's CPU.** Uncomment `C_DEFS += -DPROFILE_CPU`, build
   with the RNBO flags, `make sram`, then read `profile` over gdb. The pedal
   **boots bypassed** and `Chain::Process` early-outs, so set
   `state.bypassed_ = 0` first or the reading is meaningless — 0.4% instead of
   the real number. Three hand-written effects already cost 33.8% average /
   40.7% peak.
2. **Watch DTCMRAM.** The RNBO build sits at 92% of it. That is where the stack
   and heap live, and RNBO allocates its delay lines from the heap during
   `initialize()`. A second patch, or a bigger one, will run out — at boot, not
   politely.
3. **Decide about vendoring the export.** It is ~2.9 MB of third-party code,
   dual-licensed Cycling '74 / GPL3, currently referenced from
   `D:\Projects\test`. Committing it is a licensing decision for the owner.

## Unfinished

- **Toggles are unverified on hardware.** Two watch windows caught no movement,
  and it is not known whether they were touched. The pipeline *is* proven —
  switch → model → `Chain` → effect → coefficient, via `unit_ = 2.5806` for a
  toggle at position 2 — but **which physical switch feeds which slot** is not,
  because all three read the same position. Set them differently and check that
  TOG_SW_3 alone moves the reverb's `unit_`.
- **Toggle assignment changed** in `57f6c36`: they are now per *slot*, not per
  effect. With the default effects that means TOG_SW_1 is EQ band width where it
  used to be drive range. If it feels wrong under the hands, it is one line in
  `Chain::RunSlot`.
- **USB MIDI does not enumerate**, and it is not this code — libDaisy's own
  `seed/USB_MIDI` example fails identically on the same board and cable, while
  the chip's ROM DFU bootloader enumerates immediately. UART MIDI does
  everything USB would have. Worth an issue against the libDaisy fork.
- **Nobody has judged how it sounds.** Every DSP claim is a measurement.
- **`GenEffect`** (the gen~ adapter) has never been compiled against a real
  export. `RnboEffect` supersedes it for RNBO specifically.

## Traps this session paid for

Each of these cost real time. They are in the two READMEs in more detail.

**A monitoring loop imitates three DSP bugs at once.** An interface monitoring
inputs to outputs, with the pedal across both, makes the pedal unable to sound
fully wet, makes the drive self-oscillate, and makes the reverb sound terrible —
all at once, and none of them are the DSP. Run `make -C rig loopcheck` before
investigating any complaint about the sound. It was reported as three bugs.

**The board file has been wrong twice.** `DaisyBoonta::SetSelectLed()` crosses
green and blue and permutes the indices — both deliberate corrections for how
the LEDs are wired, and "fixing" them silently swaps drive and reverb. The
bypass relay polarity was inverted in the Template and is now corrected here:
all three relays follow `engage`, as `HardwareTest` does. Do not trust an
untested assumption from that file; `MidiControl.cpp` still takes its UART pins
on faith.

**Soft pickup will undo debugger writes.** An armed knob is rewritten from its
pot every audio block. Clearing `armed_` is not enough — pickup re-arms on
*crossing*, against a stale `entered_above_`. `rig/pedal.py` handles both; an
early version did not and silently measured the wrong state four times running.

**Do not call target functions from gdb.** `call hw.relay_input.Write(1)` runs
target code from a halted state with the audio interrupt live. Repeated, it
wedged the MCU hard enough to need a reset, after producing a run of all-zero
readings that looked like data.

**Check openocd is alive before believing a negative result.** One "nothing is
arriving" verdict came from a run where openocd had exited underneath it, and
one MIDI watch window finished before the messages were sent. Both were reported
as evidence and neither was.

## Measured numbers

| | |
|---|---|
| CPU, three effects | 33.8% average, 40.7% peak |
| SRAM, three effects | 121136 B (24.7% of 480 K) |
| SRAM, plus RNBO pitch shifter | 208812 B (42.5%), DTCMRAM 92% |
| Reverb RT60 | room 0.7–2.9 s, plate 1.4–5.1 s, cavern 2.2–10.2 s |
| EQ mid band on a 262 Hz note | −14.3 dB cut, +12.8 dB boost (rated ±15) |
| Drive harmonics, switched in | +12.3 dB across 1.5–8 kHz |
| Relays, engaged vs bypassed | +23 to +30 dB at the codec |

## Rig notes

`make -C rig <target>`: `interface`, `listen`, `verify`, `dsp`, `response`,
`relays`, `loopcheck`, `persist`, `midiprobe`, `midi`.

- Needs a source into the pedal's **input** — the interface cannot drive it, see
  the USB note. A guitar or synth is fine; measurements are relative and
  interleaved.
- Scripts that read codec levels need `C_DEFS += -DPROBE_IO` built in, and will
  say so if it is missing.
- MIDI reaches the pedal only through **`MIDIOUT3 (Ableton Push 3 MIDI)`** — the
  only one of the Push's three ports that forwards to its TRS output.
- `BOONTA_PEDAL_OUT_CH` is the interface input the pedal's output is patched to,
  zero-based, default 1.
