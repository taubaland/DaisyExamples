# Boonta Clouds Secrets — User Manual

This is Boonta Clouds with its secret layer switched on. It behaves exactly like the normal Clouds pedal until you perform a hidden gesture, which opens three extra modes: a pitch shifter and time stretcher, a looping delay, and **spectral madness**, a mode that rebuilds your sound from its frequency content and lets you mangle it.

Everything in the Clouds manual still applies (the controls, freeze, bypass, MIDI, and how the knobs and MIDI share the pedal). This manual covers what is new. The short version is in section 1.

> **Status:** this firmware has been built and tested on a computer only. It has not yet been run on a pedal. Its spectral mode in particular uses more processing than the other modes and its load on the real hardware has not been measured. If you hear crackling or dropouts in spectral mode, tell us.

## 1. The secret in one minute

1. Power up. The pedal is in **granular** mode, and only the **left select LED** is lit (violet).
2. **Press and hold the right select button (SEL 2) for five seconds.** Nothing seems to happen while you hold it.
3. After five seconds all three select LEDs **flash orange**. You are in the hidden layer, in **Stretch** mode.
4. Use the two select buttons to step through the three hidden modes.
5. To leave, hold the right select button for five seconds again. The LEDs flash orange and the pedal returns to granular.

## 2. The hidden modes

With the hidden layer open, the three select LEDs mean:

| LED | Colour | Mode |
|---|---|---|
| Left | teal | **Stretch** |
| Middle | amber | **Delay** |
| Right | crimson | **Spectral** |

The selected mode's LED glows brighter with the output level. The select buttons act when you let go of them, and they wrap around: from Spectral, next goes back to Stretch. They never return to granular. Use the five-second hold for that.

While the hidden layer is closed the select buttons do nothing.

### Stretch (teal)

One playhead, with two overlapping grains. **Density becomes the playback speed**: stopped at the minimum, normal speed at centre, double speed at the maximum. Good for freezing time or slow motion.

### Delay (amber)

The playhead is locked to real time. At centre Pitch it is a plain delay. Off centre it is a pitch-shifting delay. Position sets the delay time. Density darkens the feedback. With Freeze on, it becomes a **looper**.

### Spectral (crimson)

The sound is analysed into its frequencies, kept as snapshots, bent into new shapes, and rebuilt. With every knob at centre it is transparent: a steady note comes out at the same pitch and level, about a thirtieth of a second late. Mix it with your dry signal and that delay is not noticeable as an echo on sustained notes; on fast plucked notes you may hear it, so use Mix toward wet or use it for pads and textures.

**Every knob is neutral at centre**, and each does something different from the other modes:

| Knob | What it does in Spectral |
|---|---|
| Knob 1 — **Position** | Chooses one of six stored snapshots. Unfrozen: the slot your sound is currently being stored in. Frozen: smoothly scans between the stored snapshots. |
| Knob 2 — **Size** | Left of centre: the whole spectrum slides upward. Right of centre: the spectrum is blended with its own mirror image, ending in full reversal, where low becomes high and high becomes low. |
| Knob 3 — **Pitch** | Transposes the sound by shifting its frequencies, up to two octaves either way. |
| Knob 4 — **Density** | Left of centre: more and more frequencies stop updating, giving a partial freeze where some of the sound hangs while the rest moves on. Right of centre: successive moments are blended together, smoothing and sustaining the sound. Past about 90% random phase is added until it becomes a smeared, reverb-like wash. |
| Knob 5 — **Texture** | Left of centre: the level of each frequency is quantised, like a low-quality compressed audio file. Right of centre: the spectrum is flattened, loud partials come down and quiet ones come up, giving a breathy, noise-like version of the sound. Overall volume is held steady. |
| Knob 6 — **Mix** | Dry to wet. |
| Toggle 1 — **Spread** | No effect in this mode. |
| Toggle 2 — **Feedback** | Feeds the output back into the input. Be careful in combination with the extreme Density and Texture settings. |
| Toggle 3 — **Reverb** | As in the other modes. |
| Left footswitch — **Freeze** | Stops listening and plays back the stored snapshots. Hold to latch, as always. |
| Right footswitch | Bypass. |

The original module also had a trigger input that fired bursts of spectral glitches. The Boonta has no trigger jack, so that feature is not included.

**Spectral recipes**

- **Transparent first**: set every knob to centre and Mix to full wet. It sounds like your input. Now move one knob at a time.
- **Frozen glass**: play a chord, freeze with a long hold, set Density a little left of centre and sweep Position.
- **Robot choir**: Texture fully left, Size a little right of centre.
- **Dissolve**: Density fully right, Reverb centre, Mix about half.
- **Spectral shift**: Size left of centre for an inharmonic, bell-like upward slide.

## 3. The normal controls

In granular mode, and in the Stretch and Delay modes, the controls are the same as the normal Clouds pedal:

| Control | Function |
|---|---|
| Knob 1 — Position | Where in the recording grains are taken from (up to about 11 s back). |
| Knob 2 — Size | Grain length, 16 ms to 2 s. |
| Knob 3 — Pitch | Transposition, −2 to +2 octaves. |
| Knob 4 — Density | Grain rate and irregularity (in Stretch it is playback speed). |
| Knob 5 — Texture | Grain fade shape. |
| Knob 6 — Mix | Dry to wet. |
| Toggle 1 — Spread | Stereo scatter: mono, wide, full. |
| Toggle 2 — Feedback | Off, medium, high. |
| Toggle 3 — Reverb | Off, blended, drenched. |
| Left footswitch | Freeze. Press and hold for momentary, hold longer than ¾ of a second to latch. A quick tap while latched briefly releases freeze. |
| Right footswitch | Bypass. The pedal starts bypassed. |

LEDs: the right footswitch LED is white when the effect is in circuit. The left footswitch LED shows freeze: dim blue = recording, pulsing = frozen while held, steady = latched.

## 4. MIDI

USB (appears as *Daisy Seed Built In*) and the DIN/TRS input both work, on all channels. MIDI is not locked behind the secret gesture: you can go straight to any mode.

| CC | Controls | Values |
|---|---|---|
| 20 | Position | 0–127 |
| 21 | Size | 0–127 |
| 22 | Pitch | 0–127 (64 is about centre) |
| 23 | Density | 0–127 |
| 24 | Texture | 0–127 |
| 25 | Mix | 0–127 |
| 26 | Spread | low / centre / high thirds |
| 27 | Feedback | low / centre / high thirds |
| 28 | Reverb | low / centre / high thirds |
| 29 | Mode | four zones: Granular (0–31), Stretch (32–63), Delay (64–95), Spectral (96–127) |
| 30 | Freeze | 64 or more = frozen |
| 31 | Bypass | 64 or more = **in circuit** |

**Program Change 0, 1, 2, 3** selects Granular, Stretch, Delay, Spectral.

Choosing Stretch, Delay or Spectral over MIDI opens the hidden layer (the LEDs change to match). Choosing Granular closes it.

## 5. How the knobs and MIDI share the pedal

Every parameter can be set by a knob or by MIDI.

1. When you power on, the knobs are in charge.
2. If MIDI changes a parameter, that knob lets go. The parameter stays at the MIDI value, wherever the knob is pointing.
3. To take it back, turn the knob. Nothing happens until you have turned it a little **and** it has come within **10% of its travel** of the value the parameter now holds. Then the knob follows the parameter again.

The sound never jumps when you take a parameter back. The toggles act when you flip them, and the footswitches and select buttons only act when pressed.

## Remembering your settings

The pedal remembers where you left it. Two seconds after you stop changing something, it saves the six knob values, the three toggle positions and the playback mode, including a hidden one. When you power it on again it comes back as it was.

What is **not** remembered: bypass (the pedal always starts bypassed, so it can never surprise you with sound), and freeze.

Because the saved settings may not match where the knobs are now, the knobs start out *parked*, the same as after a MIDI change (see "How the knobs and MIDI share the pedal"):

- Each knob does nothing until you turn it a little and it comes within **10% of its travel** of the saved value. Then it takes over and follows the knob.
- Each toggle keeps its saved position until you flip it.
- Only on the very first power-up, or if the saved settings cannot be read, do the knobs take charge straight away.

Things to know:

- Settings are saved at most once every ten seconds. If you cut the power within about two seconds of a change (or ten seconds of a previous save) the very last change is lost. Nothing else is.
- Changes sent over MIDI are saved too.
- Each pedal firmware (Beads, Clouds, Clouds Secrets) ignores settings saved by the others, so switching firmware starts you at the defaults.
- If the pedal was in one of the hidden modes when you turned it off, it comes back in that mode, with the hidden layer open. The three select LEDs flash orange once at power-up to show it.


## 6. Troubleshooting

| Problem | Likely cause |
|---|---|
| The select buttons do nothing | The hidden layer is closed (the normal state). Hold the right select button for five seconds. |
| Held for five seconds and nothing flashed | The button was released early, or held for less than the full five seconds. Hold it until the orange flash, then let go. |
| Went to granular unexpectedly | A second five-second hold closes the layer. Also a MIDI CC 29 or Program Change of 0. |
| No wet sound in Spectral | Bypassed, Mix at zero, or the sound is very quiet. Spectral also starts about 30 ms late, and in frozen mode it needs something stored first. |
| Spectral sounds hollow or phasey | Normal when Density, Size or Texture are far from centre, as the sound is being rebuilt from altered data. Centre them to hear it clean. |
| Crackling or dropouts in Spectral | Spectral is the most demanding mode and has not been measured on the pedal. Turn Reverb off, and report it. |
| Feedback seems dead | Freeze is on. |
| A knob does nothing | MIDI changed that parameter. Turn the knob to within 10% of the parameter's current value. |

## 7. Installing the firmware

The firmware file is `dist/CloudsSecrets.bin` (the same build is in `CloudsSecrets/build/`). It runs through the Daisy bootloader, like Clouds and Beads.

1. Once only: with an ST-Link attached, run `make boot` in the `CloudsSecrets` folder to put the bootloader in internal flash.
2. Put the pedal into bootloader (DFU) mode.
3. Run `make program-dfu` in the `CloudsSecrets` folder.
4. Reset the pedal.

(`make sram` loads straight into RAM over the ST-Link for testing and is lost at power-off. It does not replace the normal Clouds firmware in flash.)
