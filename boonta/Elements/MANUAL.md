# Boonta Elements — User Manual

Elements is a **physical-modelling instrument**. Three exciters (a bow, a breath and a mallet) set a resonator vibrating, the way a bowed string, a blown tube or a struck bar would, and a reverb sits behind it. Your guitar goes in as a fourth exciter: every note you play can ring the resonator, and the resonator can be tuned to the note you play. It is based on Mutable Instruments Elements by Emilie Gillet.

It is not a distortion or a delay. There is **no dry signal**: what you hear is the resonator.

> **Status:** this firmware has passed its tests and been measured on the pedal, but at the time of writing nobody has played a guitar through it. Everything marked *(untested)* below has not been tried on real hardware, and the starting values are a first guess.

## 1. Getting started

1. Connect audio in and out, and power the pedal.
2. It starts **bypassed** (right footswitch LED off). Your signal passes straight through.
3. Press the **right footswitch** to put Elements in circuit. Its LED turns white.
4. Tap the **left footswitch**. You should hear a struck, bar-like note. That is the starting patch: a mallet on a resonator, no bow and no breath.
5. For the guitar to play it, set **Toggle 2** to the centre and pluck a note. The resonator rings at the pitch you played.

## 2. The pages

Elements has eighteen parameters and the pedal has six knobs, so the knobs are **paged**. The three select LEDs are the three pages. **Select buttons (◀ ▶)** go to the previous and next page.

| Page | LED | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 | Knob 6 |
|---|---|---|---|---|---|---|---|
| **Resonator** | teal | Pitch | Geometry | Brightness | Damping | Position | Space |
| **Exciters** | amber | Contour | Bow | Blow | Strike | Input | Strength |
| **Colour** | violet | Bow timbre | Flow | Blow timbre | Mallet | Strike timbre | Fine |

Which page you are on is shown by its LED, which also acts as a level meter for the output.

### Resonator page

| Knob | What it does |
|---|---|
| **Pitch** | The note, in semitones, over four octaves (C1 to C5). When following the guitar, it becomes a transposition instead: centre is unison, two octaves down to two octaves up. |
| **Geometry** | The shape of the resonating body. Moves the overtones around, from bar-like and bell-like to hollow and metallic. In the chord model it chooses the chord. |
| **Brightness** | How much high end the resonator keeps. Low is a dull thud, high is glassy. |
| **Damping** | How quickly the sound dies. Low is a long ring, high is a short pluck. |
| **Position** | Where along the resonator it is excited and listened to. It changes the balance of overtones, like plucking nearer the bridge. |
| **Space** | The stereo width and the reverb. Up to about a third of travel it widens; from the middle up it adds reverb. Fully down it lets the raw exciter through as well. |

### Exciters page

| Knob | What it does |
|---|---|
| **Contour** | The loudness shape of the note. Fully left is a pluck, fully right is a held note. In between is a swell. |
| **Bow** | How much bowed excitation. Turn it up for a sustained, scraping tone that holds as long as the gate is held. |
| **Blow** | How much breath. Past about two thirds a tube resonance joins in. |
| **Strike** | How much mallet. Past about 80% some of the raw strike bleeds straight into the output. |
| **Input** | How hard your guitar excites the resonator. Zero means the guitar is only used to trigger and tune it, not to ring it. |
| **Strength** | How hard the exciters hit. Over MIDI, key velocity scales this. |

### Colour page

| Knob | What it does |
|---|---|
| **Bow timbre** | Smooth to rough bowing. |
| **Flow** | The airflow of the breath exciter. |
| **Blow timbre** | The tone of the breath: from soft to bright. |
| **Mallet** | The mallet's hardness and speed. Low is soft felt, high is hard. |
| **Strike timbre** | The sampled strike sound: dark to bright. |
| **Fine** | Fine tuning, a semitone either side. |

### Turning the page without jumps

After a page change, every knob is pointing at a value that belongs to the parameter you just left. So the knobs are **parked**. A knob does nothing until you have turned it a little *and* it has come within **10% of its travel** of the value its new parameter holds. Then it picks the parameter up and follows it again. This means the sound never jumps when you change page, and when you come back to a page you find it as you left it.

Only the page showing at power-on takes its values from the knobs straight away. The other twelve parameters start from a playable default (or from your saved settings).

## 3. Switches

| Control | Down | Centre | Up |
|---|---|---|---|
| **Toggle 1 — Resonator** | Modal: a body with many overtones, like a bar, plate or bell | String: a single string | Strings: a chord of five strings |
| **Toggle 2 — Trigger** | Manual: only the footswitch (or MIDI) plays it | **Follow the guitar**: your playing triggers it and tunes it | Drone: the gate is held open |
| **Toggle 3 — Input to** | Strike: the guitar goes straight into the resonator | Both | Blow: the guitar goes through the diffuser, a softer, more breath-like excitation |

The **expression jack** controls Geometry once it has been moved. Nothing on the board can tell whether a pedal is plugged in, so an idle jack never takes anything.

## 4. Playing it

Three things can play a note. They add together, so none of them can cancel another.

- **The left footswitch (gate).**
- **MIDI**, a held key or CC 30.
- **Toggle 2**, either following the guitar (centre) or drone (up).

### The gate footswitch

| Gesture | Result |
|---|---|
| Press and hold | The note plays while held, and stops when you let go. |
| Hold longer than 0.7 seconds | The gate **latches** and stays open after you let go. |
| Tap while latched | The gate briefly drops while your foot is down, then re-strikes when you release. |
| Hold again while latched | Latch off. |

Gate LED: **dim amber** = nothing playing, **pulsing** = a note you are holding, **steady and bright** = latched, **steady and half bright** = a note being played by the guitar or by drone.

### Following the guitar

With Toggle 2 at the centre:

- Each note you play opens the gate, and silence closes it.
- A new attack over the end of the last note re-strikes the exciters.
- The pedal listens to the pitch you are playing and tunes the resonator to it. The sound you hear is the resonator at your note, plus its overtones (in Modal) or the other four tones of a chord (in Strings). The **Pitch** knob transposes it, so a fifth or an octave up is a setting rather than a retune.

Things to know:

- It follows **one note at a time**. A strummed chord is tracked as whichever note is strongest.
- It works from the low E (82 Hz) up to about 1 kHz. Below that it holds the last note.
- A new note takes about **30 ms** to be recognised, longer on the lowest strings. The mallet strikes at the old pitch first, and the pedal re-strikes once it knows the new one, so you may hear a faint double strike on fast playing.
- The note is rounded to whole semitones, so vibrato and bends of up to about half a tone will not wobble the resonator.
- It holds the last note through silence or noise, so a decaying note keeps its pitch.
- The tracker listens to the guitar *before* the Input knob. With Input at zero the guitar still triggers and tunes the resonator without ringing it directly. *(untested)*

To play it as a **synth**, set Toggle 2 down, turn Input down and use the footswitch or MIDI. To use it purely as a **resonator for the guitar**, turn Bow, Blow and Strike down and set Toggle 2 down, with Input up.

## 5. LEDs

| LED | Meaning |
|---|---|
| **Right footswitch** | White = in circuit, off = bypassed. |
| **Left footswitch** | Gate state, as above. |
| **Select LEDs** | The current page, with output metering. |
| **The other two select LEDs blinking red** | The sample data is missing from flash. The sampled exciters (strike and blow) will be silent. See Troubleshooting. |

## 6. MIDI

Elements listens on **USB** (it appears as *Daisy Seed Built In*) and the **DIN/TRS MIDI input** at once, on all channels. *(untested)*

| Message | Does |
|---|---|
| Note on / off | Sets the pitch and opens the gate. The last note wins. The gate closes when the last key comes up. |
| Velocity | Scales Strength while a key is down. |
| Pitch bend | ±2 semitones. |
| CC 102–107 | Pitch, Geometry, Brightness, Damping, Position, Space |
| CC 108–113 | Contour, Bow, Blow, Strike, Input, Strength |
| CC 114–119 | Bow timbre, Flow, Blow timbre, Mallet, Strike timbre, Fine |
| CC 20 / 21 / 22 | Resonator / Trigger / Input to, each in three zones |
| CC 23 | Knob page |
| Program Change 0, 1, 2 | Knob page |
| CC 30 | Gate: 64 or more is held |
| CC 31 | Bypass: 64 or more is **in circuit** |
| CC 120–127 | Releases all held notes |

Keys play around middle C. With C4 held, the Pitch knob means what it says; other keys transpose from that, and the last key played stays in force after you let go, so the footswitch replays it.

### How the knobs and MIDI share the pedal

1. **When you power on, the knobs are in charge.**
2. **If MIDI changes a parameter, that knob lets go.** The parameter stays at the MIDI value, wherever the knob points.
3. **To take it back, turn the knob** until it comes within 10% of the value the parameter now holds. It then picks the parameter up again.

The sound never jumps when you take a parameter back. A knob you do not touch never overrides MIDI. The toggles act when you flip them, and a MIDI value stands otherwise.

## 7. Sound recipes

These are starting points, not tested sounds.

- **Bell on every note:** Toggle 1 down (Modal), Toggle 2 centre, Strike up, Damping low, Brightness high. Pluck single notes.
- **Bowed guitar:** Toggle 2 centre, Contour fully right, Bow up, Strike down, Damping low. Each note you pluck then sustains as a bowed tone.
- **Shimmering fifth:** Toggle 2 centre, Pitch a little above centre (a fifth up), Space up. Hold single notes and let them ring.
- **Chord from a single note:** Toggle 1 up (Strings), Toggle 2 centre. Geometry picks the chord.
- **Breathy flute:** Blow up, Strike and Bow down, Toggle 3 up, play it from the footswitch or MIDI.
- **Drone:** Toggle 2 up, then play with Position, Geometry and the expression pedal.

## 8. Remembering your settings

The pedal is meant to save its settings two seconds after you stop changing them, and restore them at power-on, with the knobs parked as described above. Bypass is never remembered: the pedal always starts bypassed. *(untested: saving and restoring have not been seen on hardware)*

Each Boonta firmware ignores settings saved by the others, so switching firmware starts you at the defaults.

## 9. CPU and limits

- Elements is demanding. In the Modal model it uses about **30%** of the pedal's processor, about **42% at worst** while following a guitar. String and Strings use less (about 12% and 21%).
- The pedal has **64 resonator modes**, more than the 52 of the original module.
- The audio runs at **32 kHz**, because Elements is built for that rate. High frequencies above about 14 kHz are not reproduced.
- There is **no dry signal** and no output level control. All eighteen knob slots belong to Elements' own parameters.

## 10. Troubleshooting

| Problem | Likely cause |
|---|---|
| Silent | Pedal bypassed (right LED off), or Input, Bow, Blow and Strike all turned down, or Toggle 2 is on Manual and nothing is pressing the gate. |
| Guitar does not trigger it | Toggle 2 is not at the centre, or the playing is too quiet for the trigger threshold. |
| Wrong or jumpy notes | A chord, or a very low or very high note. Play single notes, and attack cleanly. |
| A knob does nothing | The knob is parked: after a page change or a MIDI change, turn it until it reaches the parameter's current value. |
| Strike or blow is silent, and the other two select LEDs blink red | The sample data is missing from QSPI flash. Re-write the full image. |
| Pedal sounds different after a firmware change | Each firmware keeps its own saved settings, so it starts from the defaults. |

## 11. Installing the firmware

The file to install is **`build/Elements-full.bin`** (not `Elements.bin`). It contains the program and the sample tables the strike and blow exciters play.

1. Once only: with an ST-Link attached, run `make boot` in the `Elements` folder to put the Daisy bootloader in internal flash. This overwrites whatever is there.
2. Write the full image to QSPI flash at `0x90040000`. On this board, DFU into QSPI has not been reliable, so use the QSPI writer: `../QspiWriter/qspi-write.sh build/Elements-full.bin`. This overwrites whatever is in QSPI.
3. Reset the pedal.

`make program-dfu` writes only the program, `Elements.bin`. That is enough once the full image has been written, since the sample tables do not change.

`make sram-bare` loads the program straight into RAM over the ST-Link, with no bootloader and no flash write. It is lost at power-off.

---

Elements is © Emilie Gillet, MIT licence. See the README for how the port is put together.
