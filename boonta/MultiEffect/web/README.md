# Web front end

An editor, a librarian and a preview for the Boonta MultiEffect, in a browser.
No build step, no dependencies: static files and ES modules.

- **Editor** — all twenty-four parameters at once, plus chain order, both kinds
  of bypass and page select. Moving a control sends the controller the firmware
  listens for.
- **Librarian** — sixteen slots mirroring the pedal's, kept in the browser and
  readable as JSON. Presets can be written onto the hardware, exported as files
  and imported back.
- **Preview** — the firmware's DSP ported to an AudioWorklet, running on a
  built-in stimulus, a file of yours, or live input.

## Running it

```bash
make serve
```

then open <http://localhost:8123>.

It has to be served rather than opened as a file. ES modules will not load over
`file://`, and both Web MIDI and `getUserMedia` need a secure context — which
`localhost` counts as, so there is no certificate to deal with.

Leave the server running. The preview's DSP is an AudioWorklet, and a worklet
module is fetched at the moment you first press **Play** — not when the page
loads. A tab that outlives its server looks completely healthy until then, and
`Play` is where it fails. If the preview reports that it could not load the
worklet, that is what happened: restart `make serve` and press Play again.

**Chrome, Edge or Opera.** Web MIDI is not in Safari or Firefox. Everything
except sending works in those; the preview is plain Web Audio.

## Hosting it

There is nothing to build, so any static host will do, and HTTPS gets you the
secure context that Web MIDI and `getUserMedia` need. GitHub Pages is set up in
[`.github/workflows/pages.yml`](../../../.github/workflows/pages.yml).

Pages can only deploy a branch's root or its `/docs`, and this is neither, so
the workflow uploads the directory as the Pages artifact rather than moving the
page out to a repository of its own. That is deliberate: `js/midi.js` and
`js/params.js` are transcriptions of `MidiMap.h` and the effect sources, and
they have to be reviewed against them. A copy in another repository is a copy
that drifts.

Two one-time steps, which only a repository admin can do — the Actions tab,
enable workflows (a fork has them off); then Settings → Pages → Source: GitHub
Actions. It lands at `https://<owner>.github.io/<repo>/`. Every path in the app
is relative, including the worklet, which resolves from `import.meta.url`, so
the extra path prefix costs nothing.

**Your bank does not travel with you.** The sixteen slots live in
`localStorage`, which is per origin, so `http://localhost:8123` and
`https://<owner>.github.io` each have their own and neither can see the other.
Moving from one to the other means **Export bank** on the first and **Import
file** on the second. The same applies to a private-window session, and to
clearing site data.

## Talking to the pedal

Press **Enable MIDI**, then choose an output. Either route works: the Daisy's own
micro-USB, or an interface with a MIDI out patched to the pedal's TRS/DIN input.

USB is the better one: one cable, no merge box, and the pedal shows up as
**"Daisy Seed Built In"** in the port list. It did not work at first — a stale
Zadig driver on the host was claiming libDaisy's stock VID/PID and Windows
loaded a serial driver on a MIDI device. Fixed by moving the PID;
[the parent README](../README.md#midi) has the diagnosis and the numbers. If
your pedal does not appear, that is the first thing to check, and the DIN route
works meanwhile.

If you do not know which port reaches the pedal, press **Probe ports**. It walks
every output in turn sending page select, which moves the page LED and touches
nothing you can hear, and names each port as it tries it. Watch the pedal and
pick the one it reacted on. Same idea as [`../rig/midi_probe.py`](../rig/midi_probe.py),
and it exists for the same reason: on a rig where the pedal is fed from a
controller's MIDI out, only one of the several ports the computer offers
actually forwards to it, and nothing about the names says which.

The channel selector is there for completeness. `midimap::kChannel` is omni by
default, so the pedal answers on any of them.

### The traffic is one way

The firmware has a MIDI in and no out — no SysEx dump, no CC echo — so **nothing
here can read the pedal back**. Everything on screen is what has been *sent*,
never what has been received. Two things follow:

- A knob turned on the pedal changes the pedal and not this, and there is no way
  for the browser to find out. The two agree again after a push.
- The bank here is one *you* keep. It is not a view of what is in the pedal's
  flash. The slot marked `PEDAL` is only the last one this app sent a program
  change for.

### Soft pickup will undo a controller

A knob that has been picked up on the pedal is rewritten from its pot on every
audio block, so a controller change to that parameter lasts about a millisecond.
The hand on the pedal beats the controller across the room — deliberately, and
it falls out of soft pickup rather than being special-cased. If a parameter will
not move from here, that is almost always why: the pedal is sitting on that page
with a hand on that knob. Leave it alone and the remote value stands.

## Presets

Four operations, and the difference between them matters.

| Button | What it does |
|---|---|
| **Load into editor** | browser only — pulls a slot into the editor, and pushes it to the pedal if *send edits* is on |
| **Store here** | browser only — snapshots the editor into a slot |
| **Recall on pedal** | one program change. The pedal loads *its* slot, which need not be what the browser holds |
| **Write to pedal** | program change, then every controller, so the pedal saves this sound into that slot |

**Write to pedal** works the way it does because there is no save gesture in the
firmware. The live settings are written back into whichever preset is current,
so writing slot *n* means recalling *n* first and then sending the parameters —
they become the live settings, and so become *n*. Recalling first is not
optional: without it the parameters land in whatever slot happened to be
current, quietly overwriting it.

Then leave the pedal alone for a couple of seconds. The save is debounced by
`kSettleMs`, and a knob touched inside that window is what gets committed
instead. The panel says how long to wait.

**Recall on pedal** deliberately leaves the editor alone, so you can hear what
the pedal has in a slot and compare it against what the browser thinks is there.

Presets export as JSON — one file per preset, or the whole bank. The format is
the fields of `SavedState` plus a name, which is the one thing a pedal with
sixteen unlabelled slots and no screen cannot store for you.

## The preview

`worklet/boonta-processor.js` is a port of `Chain.cpp`, `EqEffect.cpp`,
`DriveEffect.cpp`, `ReverbEffect.cpp` and `Biquad.h` — same topology, same
coefficients, same order of operations, and the same 128-frame block, which is
both the firmware's `kMaxBlockSize` and the Web Audio render quantum.

What it is not:

- Not the codec, the relays or the analog path. Nothing in this repository
  describes the pedal's input and output stages, so none of it is modelled.
- Not sample-identical. The firmware runs at 48 kHz and this runs at whatever
  the browser's audio context is. Everything is a function of the sample rate,
  as on the hardware, so the sound tracks — but a tank whose lengths round to a
  different grid is not the same tank.

Close enough to choose a sound with; not close enough to measure with.
[`../rig`](../rig) is what measures.

Measured on this port, for what it is worth: the low shelf gives ±13.1 dB at
60 Hz with the corner at 141 Hz, hard bypass is bit-exact pass-through, and the
reverb decays smoothly across the TIME range.

### Signals

Live input is the honest one, and the only one that is really yours. Everything
the browser normally does to a microphone — echo cancellation, noise
suppression, automatic gain control — is turned off, because on a guitar those
gate the tail of every note and undo the input level you came here to set. **Use
headphones**: the preview goes to your speakers.

The built-in stimuli are Karplus-Strong plucks, generated rather than shipped.
They are not a guitar, but they have the two properties that matter for setting
a pedal up: a transient with real high-frequency content for the drive to bite
on, and a decaying harmonic tail for the reverb to hang off.

The sweep and the noise are for the EQ, where a note tells you almost nothing —
a filter curve read off a spectrum with signal in only one band is a measurement
of the noise floor moving. [`../rig/README.md`](../rig/README.md) has the
version of that lesson that cost a day.

Or load a file. Anything the browser can decode.

### The toggles

The three-position switches are not in the MIDI map and not in a preset, and
cannot be: they are physical switches, so their position at power-on *is* the
truth. The panel is a statement of where yours are set, so the preview knows
which drive range, reverb size and band width to run. Importing a preset records
the positions it was made with but does not apply them — a preview that silently
disagreed with the hardware, in the one place you are listening to decide
whether a sound is right, would be worse than no preview.

## Layout

| File | Role |
|---|---|
| `js/params.js` | what the parameters are, and what a normalised value means |
| `js/state.js` | **model** — the same shape as `PedalState`, plus preset (de)serialisation |
| `js/midi.js` | **controller, remote** — the map from `MidiMap.h`, and the port |
| `js/presets.js` | the bank, local storage, and the write-to-pedal sequence |
| `js/audio.js` | the preview graph |
| `js/source.js` | stimulus: plucks, sweep, noise, file, live input |
| `js/ui.js` | knob and segmented-button widgets |
| `js/app.js` | wiring only, the same job as `MultiEffect.cpp` |
| `worklet/boonta-processor.js` | the DSP port |

The dependencies point one way, as they do in the firmware: an edit writes the
model, and the model is pushed at the MIDI port and at the preview. Neither
writes back.

## What would need firmware changes

Everything here is read-only with respect to the pedal, because that is all the
firmware allows. A SysEx dump request — "send me your bank" — plus a reply on
the MIDI out would make this a real two-way editor: the bank could be read off
the hardware, an edit made on the pedal could be pulled back, and the slot
labelled `PEDAL` would be a fact rather than a guess. That is a change to
`MidiControl` and a new transmit path, not a change here.
