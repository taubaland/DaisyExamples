# Boonta Beads — User Manual

Beads turns your guitar (or any audio) into clouds of tiny sound fragments called *grains*. The pedal is always recording the last twenty-two seconds of what you play. Grains are drawn from that recording, each with its own length, pitch and position, and mixed back in with your dry signal. Beads leans toward **percussive, plucky and reversed** textures, and toward deliberately gritty ones.

## 1. Getting started

1. Connect audio in and out, and power the pedal.
2. The pedal starts **bypassed** (right footswitch LED off). Your signal passes straight through.
3. Press the **right footswitch** to put Beads in circuit. Its LED turns white.
4. Play something, then turn **Mix** (Knob 6) up. Nothing is heard until the buffer has a little audio in it. It records even while bypassed, so there is usually something ready.
5. Turn **Density** (Knob 4) up and **Size** (Knob 2) down for a spray of short grains.

## 2. Controls at a glance

| Control | What it does |
|---|---|
| Knob 1 — **Time** | How far back in the recording grains are taken from, 20 ms to 20 s. |
| Knob 2 — **Size** | Grain length, 10 ms to 1 s. Short is clicky; long is smooth and washy. |
| Knob 3 — **Pitch** | Grain pitch, −2 to +2 octaves. Centre is no change. |
| Knob 4 — **Density** | Grains per second, 1 to 150. Low is sparse and rhythmic; high is a continuous cloud. Near the bottom the spacing is loose and random; higher up it becomes steadier and more clocked. |
| Knob 5 — **Shape** | Grain envelope. Fully left is a pluck (instant attack, decay). Centre is a soft triangle. Fully right is reversed (slow rise, abrupt end). |
| Knob 6 — **Mix** | Dry to wet. |
| Toggle 1 — **Seed** | Random scatter per grain (pitch, stereo position, start point). Down: none. Centre: ±3 semitones. Up: ±12 semitones. |
| Toggle 2 — **Feedback** | How much of the output is fed back into the recording. Down: off. Centre: medium. Up: high (builds long trails). |
| Toggle 3 — **Quality** | Character of the recording. Down: clean. Centre: warm, mild saturation. Up: lo-fi (8-bit, quarter sample rate). This is recorded into the loop, so frozen audio keeps the crunch. |
| Select buttons (◀ ▶) | Step the **pitch quantiser**: Free, Chromatic, Harmonic. |
| Left footswitch | **Freeze**. |
| Right footswitch | **Bypass** on/off. |
| Expression jack | Controls Pitch once moved. |

## 3. Freeze

Freeze stops the recording, so the grains keep playing from a fixed snapshot.

| Gesture | Result |
|---|---|
| Press and hold | Freeze while held. Let go and it releases. A quick tap gives a stutter. |
| Hold longer than ¾ of a second | Freeze **latches**. It stays on after you let go. |
| Hold again (while latched) | Latch turns off, and freeze drops as soon as the threshold is crossed. |
| Quick tap while latched | Freeze briefly **releases** while your foot is down, so new audio is recorded. This is how you add a new phrase to a held drone. |

Freeze LED: **dim blue** = recording normally, **pulsing blue** = frozen while held, **steady blue** = latched.

While frozen, Feedback has no effect (nothing is being recorded).

## 4. Pitch quantiser

Press the select buttons to choose how grain pitch is rounded. The quantiser is applied *after* Seed's random scatter, so you can scatter widely and still stay musical.

| Setting | LED | Result |
|---|---|---|
| Free | pale blue | Any pitch. |
| Chromatic | green | Nearest semitone. |
| Harmonic | gold | Only unison, fourth, fifth and octave. |

The lit select LED also acts as a **level meter**: it breathes with the output.

## 5. LEDs

- **Right footswitch LED**: white = effect in circuit, off = bypassed. It tints **red** as the recording input nears clipping. Beads sounds good driven, so a little red is fine, but a lot of it means the input is too hot.
- **Left footswitch LED**: freeze state (above).
- **Select LEDs**: the quantiser setting, with output metering.

## 6. MIDI

Beads listens on **USB** (it appears as *Daisy Seed Built In*) and the **DIN/TRS MIDI input** at the same time. It responds on all channels.

| CC | Controls | Values |
|---|---|---|
| 20 | Time | 0–127 |
| 21 | Size | 0–127 |
| 22 | Pitch | 0–127 (64 is about centre) |
| 23 | Density | 0–127 |
| 24 | Shape | 0–127 |
| 25 | Mix | 0–127 |
| 26 | Seed | 0–42 down, 43–85 centre, 86–127 up |
| 27 | Feedback | same three zones |
| 28 | Quality | same three zones |
| 29 | Pitch quantiser | three zones: Free / Chromatic / Harmonic |
| 30 | Freeze | 64 or more = frozen, below 64 = released |
| 31 | Bypass | 64 or more = **in circuit**, below 64 = bypassed |

**Program Change 0, 1, 2** selects Free, Chromatic, Harmonic.

Other controllers (modulation, volume and so on) are ignored. Every parameter on the pedal is covered, so a controller can do everything your hands can. The same CC numbers are used on the Clouds pedal, so one controller template drives both.

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

- **Pluck cloud**: Shape fully left, Size short, Density medium, Seed centre, Quantise Harmonic.
- **Reverse swell**: Shape fully right, Size long, Feedback centre, Mix about half.
- **Shimmer**: Pitch up an octave, Feedback up, Seed centre, Quantise Chromatic.
- **Broken tape**: Quality up, Density low, Time long, then freeze a good moment.
- **Drone pad**: play a chord, hold the left footswitch to latch freeze, then play over it.

## Remembering your settings

The pedal remembers where you left it. Two seconds after you stop changing something, it saves the six knob values, the three toggle positions and the pitch quantiser setting. When you power it on again it comes back as it was.

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
| No wet sound | Pedal bypassed (LED off), Mix at zero, or the pedal was only just powered and has little recorded. |
| Knob does nothing | MIDI changed that parameter. Turn the knob to within 10% of the parameter's current value. |
| Sound is distorted | Input too hot (red bypass LED). Lower Quality or reduce the input level. |
| Feedback seems dead | Freeze is on. |
| Expression pedal ignored | Move it through some of its travel first. |

## 9. Installing the firmware

The firmware file is `dist/Beads.bin` (the same build is in `Beads/build/`). Beads runs through the Daisy bootloader.

1. Once only: with an ST-Link attached, run `make boot` in the `Beads` folder to put the bootloader in internal flash.
2. Put the pedal into bootloader (DFU) mode.
3. Run `make program-dfu` in the `Beads` folder. The program is written to QSPI flash.
4. Reset the pedal.

(`make sram` loads straight into RAM over the ST-Link for testing, without a flash write; it is lost at power-off.)
