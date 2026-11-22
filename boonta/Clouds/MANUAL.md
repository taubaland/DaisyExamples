# Boonta Clouds — User Manual

Clouds turns your audio into a soft cloud of tiny sound fragments called *grains*. The pedal is always recording the last eleven seconds of what you play; grains read from that recording, are faded in and out, and are mixed with your dry signal. A built-in reverb finishes the sound. Clouds leans toward **smooth, atmospheric** textures, and also works as a time-stretcher and a delay.

## 1. Getting started

1. Connect audio in and out, and power the pedal.
2. The pedal starts **bypassed** (right footswitch LED off).
3. Press the **right footswitch** to put Clouds in circuit. Its LED turns white.
4. Play, then turn **Mix** (Knob 6) up. It records even while bypassed, so there is usually something ready. After a fresh power-up, give it a second or two of playing before it has anything to read.
5. Turn **Size** up and **Density** up for a thick, smooth cloud.

## 2. Controls at a glance

| Control | What it does |
|---|---|
| Knob 1 — **Position** | Where in the recording grains are taken from, from just-played to about 11 seconds ago. |
| Knob 2 — **Size** | Grain length, 16 ms to 2 s. |
| Knob 3 — **Pitch** | Transposition, −2 to +2 octaves. Centre is no change. |
| Knob 4 — **Density** | Grain rate and how irregular the spacing is. In Stretch mode it becomes playback speed (see below). |
| Knob 5 — **Texture** | Shape of each grain's fade: nearly square at one end, all curve at the other. |
| Knob 6 — **Mix** | Dry to wet. |
| Toggle 1 — **Spread** | Stereo scatter. Down: mono. Centre: wide. Up: full stereo scatter. |
| Toggle 2 — **Feedback** | Output fed back into the recording. Down: off. Centre: medium. Up: high. |
| Toggle 3 — **Reverb** | Down: off. Centre: blended. Up: drenched. |
| Select buttons (◀ ▶) | Previous / next **playback mode**. |
| Left footswitch | **Freeze**. |
| Right footswitch | **Bypass** on/off. |
| Expression jack | Controls Position once moved. |

## 3. Playback modes

Press the select buttons to change mode. All modes share the same recording and the same knobs; they differ in how grains are picked.

| Mode | LED | What it does |
|---|---|---|
| **Granular** | violet | Random start points, spacing and panning: the classic cloud. |
| **Stretch** | teal | A single playhead with two overlapping grains. **Density becomes playback speed**: stopped at minimum, normal speed at centre, double speed at maximum. Good for freezing time and slow motion. |
| **Delay** | amber | Playhead locked to real time. At centre Pitch it is a plain delay (Position = delay time); off centre it is a pitch-shifting delay. Density damps the feedback path. With Freeze on, it becomes a **looper**. |

The lit mode LED doubles as a **level meter**, which helps tell an empty buffer from a Mix knob that is turned down.

## 4. Freeze

Freeze stops recording, so grains keep playing from a fixed snapshot.

| Gesture | Result |
|---|---|
| Press and hold | Freeze while held. Release to let go. A quick tap is a stutter. |
| Hold longer than ¾ of a second | Freeze **latches**. It stays on after you let go. |
| Hold again (while latched) | Latch turns off, and freeze drops as soon as the threshold is crossed. |
| Quick tap while latched | Freeze briefly **releases** while your foot is down, so you can record a new phrase into a held drone. |

Freeze LED: **dim blue** = recording normally, **pulsing blue** = frozen while held, **steady blue** = latched. While frozen, Feedback has no effect.

## 5. LEDs

- **Right footswitch LED**: white = in circuit, off = bypassed.
- **Left footswitch LED**: freeze state (above).
- **Select LEDs**: current playback mode, with output metering.

## 6. MIDI

Clouds listens on **USB** (it appears as *Daisy Seed Built In*) and the **DIN/TRS MIDI input** at the same time, on all channels.

| CC | Controls | Values |
|---|---|---|
| 20 | Position | 0–127 |
| 21 | Size | 0–127 |
| 22 | Pitch | 0–127 (64 is about centre) |
| 23 | Density | 0–127 |
| 24 | Texture | 0–127 |
| 25 | Mix | 0–127 |
| 26 | Spread | 0–42 down, 43–85 centre, 86–127 up |
| 27 | Feedback | same three zones |
| 28 | Reverb | same three zones |
| 29 | Playback mode | three zones: Granular / Stretch / Delay |
| 30 | Freeze | 64 or more = frozen, below 64 = released |
| 31 | Bypass | 64 or more = **in circuit**, below 64 = bypassed |

**Program Change 0, 1, 2** selects Granular, Stretch, Delay.

Other controllers (modulation, volume and so on) are ignored. Every control on the pedal is covered, and the same CC numbers are used on the Beads pedal, so one controller template drives both.

## How the knobs and MIDI share the pedal

Every knob, switch and button on the pedal can also be controlled over MIDI, so
two things can try to set the same parameter. The rule is simple:

1. **When you power on, the knobs are in charge.** Each parameter follows its knob.
2. **If MIDI changes a parameter, that knob lets go.** The parameter stays at the MIDI value, wherever the knob is pointing.
3. **To take it back, turn the knob.** Nothing happens until you have turned it a little *and* it has come within **10% of its travel** of the value the parameter now holds. At that point the knob picks the parameter up and follows it again.

The sound never jumps when you take a parameter back, because the knob only starts following from a point close to where the parameter already is. A knob you do not touch never overrides MIDI.

The three toggle switches work the same way in spirit: they act when you flip them, and otherwise a MIDI value stands. The footswitches and select buttons only act when pressed.

> Expression pedal: it has to be *moved* before it takes its parameter, and then the same 10% rule applies. Nothing on the board can tell whether a pedal is plugged in, so an idle jack never grabs anything.

## 7. Sound recipes

- **Ambient wash**: Granular, Size long, Density high, Spread centre, Reverb up, Mix about half.
- **Slow motion**: Stretch, Density just below centre, Size long, Reverb centre.
- **Octave-up echo**: Delay, Pitch up an octave, Feedback centre, Position for the delay time.
- **Looper**: Delay, play a phrase, hold the left footswitch to latch freeze, play over it.
- **Frozen pad**: Granular, latch freeze on a chord, then sweep Position and Texture.

## Remembering your settings

The pedal remembers where you left it. Two seconds after you stop changing something, it saves the six knob values, the three toggle positions and the playback mode. When you power it on again it comes back as it was.

What is **not** remembered: bypass (the pedal always starts bypassed, so it can never surprise you with sound), and freeze.

Because the saved settings may not match where the knobs are now, the knobs start out *parked*, the same as after a MIDI change (see "How the knobs and MIDI share the pedal"):

- Each knob does nothing until you turn it a little and it comes within **10% of its travel** of the saved value. Then it takes over and follows the knob.
- Each toggle keeps its saved position until you flip it.
- Only on the very first power-up, or if the saved settings cannot be read, do the knobs take charge straight away.

Things to know:

- Settings are saved at most once every ten seconds. If you cut the power within about two seconds of a change (or ten seconds of a previous save) the very last change is lost. Nothing else is.
- Changes sent over MIDI are saved too.
- Each pedal firmware (Beads, Clouds, Clouds Secrets) ignores settings saved by the others, so switching firmware starts you at the defaults.


## 8. Troubleshooting

| Problem | Likely cause |
|---|---|
| No wet sound | Pedal bypassed (LED off), Mix at zero, or little audio recorded since power-up. |
| Knob does nothing | MIDI changed that parameter. Turn the knob to within 10% of the parameter's current value. |
| Feedback seems dead | Freeze is on. |
| Expression pedal ignored | Move it through some of its travel first. |
| Very long tail | Feedback and Reverb both at maximum build a very large tail; lower one. |

## 9. Installing the firmware

The firmware file is `dist/Clouds.bin` (the same build is in `Clouds/build/`). Clouds runs through the Daisy bootloader.

1. Once only: with an ST-Link attached, run `make boot` in the `Clouds` folder to put the bootloader in internal flash.
2. Put the pedal into bootloader (DFU) mode.
3. Run `make program-dfu` in the `Clouds` folder. The program is written to QSPI flash.
4. Reset the pedal.

(`make sram` loads straight into RAM over the ST-Link for testing; it is lost at power-off.)
