# Hardware measurement rig

Measures the real pedal: what reaches its codec, what leaves its output jack,
and whether the DSP is doing what it claims. Where [`../test`](../test) checks
the DSP on a host with no hardware at all, this checks the thing on the bench.

It exists because a pedal that makes no sound gives you almost nothing to go on.
This tells you *which side* of the codec the silence is, which is the difference
between a firmware bug and a cable.

## What you need

```
source (guitar) --> pedal IN
pedal OUT --------> interface input
```

Plus openocd serving gdb on `:3333`, and a `PROBE_IO` build on the board.

The rig deliberately does **not** need the interface to play anything. Driving
audio *into* the pedal from the computer turned out to be the one thing the
Push 3 could not do, so the stimulus is whatever you plug into the pedal. A
guitar works: every measurement here is relative and interleaved, so a stimulus
that wanders in level averages out.

Enable the probe in [`../Makefile`](../Makefile):

```make
C_DEFS += -DPROBE_IO
```

then rebuild and reflash. Without it the scripts that read codec levels will say
the symbol is missing. Start openocd:

```bash
openocd -s "/c/Program Files/DaisyToolchain/openocd/scripts" -f interface/stlink.cfg -f target/stm32h7x.cfg
```

## Running

```bash
make venv
```

```bash
make interface
```

| target | what it does |
|---|---|
| `make interface` | list devices, show which configurations open, scan every input |
| `make listen` | what is arriving at the interface *and* at the codec, together |
| `make verify` | regression check on the relay logic — engaged must feed the codec, bypassed must cut it |
| `make dsp` | A/B the EQ shelves and the drive, measured at the pedal output |
| `make response BAND=mid` | measure one EQ band's gain at the note being played (`low`/`mid`/`high`) |
| `make relays` | sweep all eight relay states, measuring both ends |
| `make loopcheck` | is something outside the pedal returning its output to its input? **Run this first** for any complaint about the sound |
| `make persist` | write a distinctive state, reset the MCU, check it comes back — needs no audio source |
| `make midiprobe` | find which MIDI output actually reaches the pedal |
| `make midi PORT="name"` | send CCs and program changes, check the model followed — needs no audio source either |

Devices are found by **name**, not index, because indices move whenever another
device is plugged in. Override with environment variables:

| variable | default | meaning |
|---|---|---|
| `BOONTA_DEVICE` | `Push` | substring matched against device names |
| `BOONTA_HOSTAPI` | `WASAPI` | preferred host API |
| `BOONTA_IN_DEVICE` | *(auto)* | force an input device index |
| `BOONTA_OUT_DEVICE` | *(auto)* | force an output device index |
| `BOONTA_PEDAL_OUT_CH` | `1` | which input channel the pedal's output is on, zero-based |

## The pieces

| File | Role |
|---|---|
| `rig.py` | audio primitives — capture, RMS/peak, Goertzel, THD, device discovery |
| `pedal.py` | drives the pedal's model over the ST-Link, so tests need no knob-twiddling |
| `interface.py` | device and channel diagnostics |
| `listen.py` | both ends at once: interface inputs and codec probe |
| `verify.py` | relay regression check |
| `dsp.py` | DSP A/B over fixed bands |
| `response.py` | one EQ band's gain, measured at the stimulus frequency |
| `relays.py` | eight-state relay sweep |
| `loopcheck.py` | external feedback loop detection |
| `persist.py` | saved-settings round trip across a reset |
| `midi_probe.py` | which MIDI port reaches the pedal, if any |
| `midi_test.py` | CC and program-change control, checked against the model |

## Two traps worth knowing

**Soft pickup will undo your writes.** An armed knob is rewritten from its pot
every audio block, so setting a parameter the pot owns lasts about a
millisecond. Clearing `armed_` is not enough either — pickup also re-arms on
*crossing*, comparing against `entered_above_`, which still holds whatever was
true when the page was last entered. `pedal.setup()` writes both, which is why
values stick. An early version of this rig did not, and quietly measured the
wrong state four times in a row.

**Do not call target functions from gdb.** `call hw.relay_input.Write(1)` is an
inferior call: it runs code on the target from a halted state, with the audio
interrupt live. Doing it repeatedly wedged the MCU hard enough to need a reset,
after producing one run of all-zero readings that looked like data. Everything
here reads and writes memory instead. `relays.py` is the exception and the
reason it is a diagnostic rather than something to run casually.

## What it found

Worth recording, because both were mine and neither was visible from the code:

- **The bypass relay polarity was inverted**, inherited from the Template.
  De-energised, that relay closes an analog path from input jack to output jack,
  so the "engaged" state summed dry signal with the DSP output and partially
  cancelled it. `verify.py` is the test that pins this down: engaged reads
  −24.7 dBFS at the codec against −54.9 bypassed, a 30 dB difference.
- **CPU load was ten times my estimate** — 33.8% average, 40.7% peak, measured
  with `PROFILE_CPU` rather than guessed.

## Finding the MIDI route

`midiprobe` exists because "send MIDI at it and see" is not one question but
several. On the setup this was written against, the pedal's TRS MIDI input is
fed from a Push 3, and only **one of the three Push MIDI ports** the computer
offers actually forwards to the Push's own MIDI output. The other two are
silent, as is the system synth. Nothing about the names says which.

Two things make the probe trustworthy where a bare "did it work" does not:

- It counts **received** separately from **accepted**, so a controller sending
  numbers this pedal does not claim still proves the link is alive.
- It sends to every port in turn and reports each, rather than testing one and
  concluding from silence.

Note also that a picked-up knob overwrites a remote change on the next audio
block -- the hand beats the controller, by design -- so `midi_test.py` parks the
knobs first. Without that the map looks broken when it is working exactly as
intended.

## The first question to ask about how it sounds

Run `make loopcheck` before investigating any complaint about the sound. An
interface monitoring its inputs through to its outputs, with the pedal patched
across both, closes a loop through the pedal — and that one fault imitates three
DSP bugs at once, convincingly enough that all three were reported together:

- **the pedal never sounds fully wet**, because recirculated dry signal is
  present at its *input*, and is therefore inside the wet signal where no mix
  control can reach it;
- **the drive self-oscillates before it affects anything**, because it does —
  its gain takes the loop past unity;
- **the reverb sounds terrible**, because it is inside a feedback loop.

The check switches every effect out, reducing the chain to
`out = in * trim * level`: pure feedforward, no state, no feedback, and
mathematically incapable of oscillating. It then sweeps the two gains and
compares each reading against what those gains predict. A feedforward path must
track them proportionally; a loop will not. That comparison is what makes it
work whether or not a source is playing — looking for "loud" would call a loud
guitar a feedback loop.

The evidence that settled it, with all three effects switched out:

```
in 0.5, out 0.8  ->   0.0 dBFS
in 1.0, out 0.2  ->  -7.9 dBFS
```

Corroborated by switching effects in one at a time: *every* one pinned the output
at full scale from no input, **including a flat EQ** — which is a bit-exact
passthrough, since at 0 dB the RBJ numerator and denominator are identical.
Three unrelated effects failing identically, one of which does nothing at all, is
the signature of a fault outside them.

## A third trap: measure where the signal is

A sustained note only tells you about the frequencies it contains. Reading a
filter curve off the rest of the spectrum measures the noise floor moving, and
does it convincingly: an early `response.py` drew a "high shelf" of +11 dB flat
across every third-octave band from 40 Hz to 10 kHz, because the only band with
real signal in it was the one at the fundamental.

Two rules came out of that, and both are in `response.py` now:

- **Tune the band under test onto the note**, and measure only there.
- **Switch the other effects out.** The reverb's tail runs for seconds, so
  flipping from boost to cut leaves the louder previous state still decaying
  into the capture. That floors the cut reading — measured −7.3 dB against a
  rated −15 — while leaving the boost looking correct, which reads as an
  asymmetric EQ rather than as contamination. With drive and reverb switched
  out, the same band measures −14.3 / +12.8 dB, a 27.1 dB sweep against a rated
  30.

For a genuine curve you need a broadband stimulus — noise or a sweep into the
pedal's input, which needs an interface that can actually drive it.
