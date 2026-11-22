# Djin handover

Written 22 Nov 2026, at the end of a long bring-up session. Read this first when
picking the project up again. `README.md` in this folder describes the firmware
itself; this file is about the state of the work, the environment, and what to
do next.

## Where things stand

- Djin runs on the Sunhaus pedal (Daisy Seed). Audio passes, Script One works
  and the menu encoder changes the slice length immediately. Both encoders, the
  menu switch and the footswitch work.
- The last thing confirmed by ear: audio passing with the slice-length control
  live. **Not yet judged by ear:** the Main encoder (direction, randomness,
  half-speed slices), the Clean/Blend and Volume knobs, the footswitch bypass,
  mono detection, and how the slice length range feels.
- The LED never lights, because the pedal board has a **short to ground in the
  LED circuit** (see Known issues). The firmware drives the pin correctly.
- Scripts 2-6 are pass-through placeholders. Only the PDF's Script One is built.

## Repository state

- Branch `boonta-midi-presets`. Commit `723f190` adds `sunhaus/Djin/` and bumps
  the `libDaisy` submodule.
- The `libDaisy` submodule is on a local branch **`sunhaus-board`** (commit
  `fa820f6` on top of `638aa09`, on top of `6f1dc70`). **These commits are not
  pushed anywhere.** Push `sunhaus-board` from inside `libDaisy/` before pushing
  this repo, or the submodule pointer will name a commit nobody else can fetch.
- Untracked, deliberately left out: `sunhaus/Scripts 1 + global.pdf` (the spec)
  and `sunhaus/Stereo Daisy 2.png` (the schematic).
- Nothing is pushed.

## Toolchain and flashing (the important part)

**Build with Arm GCC 12.x. Do not use GCC 15.3.** With 15.3 the Seed's codec
delivered exactly zero input samples and no output, even for the stock
`seed/bypass` example. 12.2 fixed it. The original Daisy toolchain in
`/Library/DaisyToolchain` is x86_64 and will not run on this Mac (no Rosetta).

```bash
export PATH=$HOME/arm-gnu-toolchain-12.2.rel1/bin:$PATH
# libDaisy and DaisySP must be built with the same compiler. After switching
# compilers, clean first:
make -C libDaisy clean && make -C libDaisy -j8
make -C DaisySP clean  && make -C DaisySP -j8
cd sunhaus/Djin && make clean && make
```

Flash over the ST-Link with openocd (installed with `brew install open-ocd`):

```bash
cd sunhaus/Djin
/opt/homebrew/bin/openocd -s $(brew --prefix open-ocd)/share/openocd/scripts \
  -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program build/Djin.elf verify reset exit"
```

- DFU does not work on these boards; the ST-Link is the only way in.
- Djin is an internal-flash build (104 KB of 128 KB, about 80%). Flashing it
  **overwrites the Daisy bootloader**. Beads (and the other `boonta` firmwares)
  need the bootloader; restore it with `make boot` in a `boonta` firmware folder.
  Beads was flashed to QSPI on one Seed earlier in the session.
- Use `/opt/homebrew/bin/openocd`. The one in `/usr/local/bin` is x86_64.
- If openocd says "target voltage may be too low", the Seed is not powered:
  plug in the 9V adapter.
- Connecting the ST-Link resets the Seed. `kBootIntoEffect = true` (in
  `Djin.cpp`) makes Djin start straight in Effect 1 so a reset does not leave
  it silent. Set it to `false` for the spec's sleep-on-startup behaviour when
  the pedal is finished.
- `pkill -9 openocd` can leave a stale ST-Link claim; wait a couple of seconds
  before the next openocd run, and expect one `init failed` retry.

## Hardware (from the schematic, confirmed against the firmware)

| Pin | Seed | Function |
|---|---|---|
| 11 | D10 / PB5 | Menu encoder push switch |
| 14 | D13 / PB6 | Footswitch |
| 22 | D15 / PC0 | Volume pot |
| 23 | D16 / PA3 | Clean/Blend pot |
| 24 / 25 | D17 PB1 / D18 PA7 | Menu encoder A / B |
| 27 / 26 | D20 PC1 / D19 PA6 | Main encoder A / B |
| 28 | D21 / PC4 | LED through R4 (1k). **No timer channel**, so the LED is software PWM |
| 16-19 | | Audio in L/R, out L/R (Seed codec is the AK4556, board-ID pins both high) |

The analog input stage (TL072 buffer, 22k / 10k divider) takes about 10 dB off,
so a guitar arrives at about -36 dBFS peak. `kInputGain = 6` restores it.

## Encoders: do not "simplify" this

libDaisy's `Encoder` missed clicks on both encoders, so `DaisySunhaus` has its
own `QuadEncoder` (in `libDaisy/src/daisy_sunhaus.{h,cpp}`):

- **Menu encoder** rests alternately at 00 and 11 (half-cycle detent). One half
  of each cycle has **no visible middle state** (A and B change within 31 us).
  The decoder counts once per arrival at the *other* rest state; direction comes
  from the middle state seen, and a bare 00 to 11 jump is +, 11 to 00 is -.
  Transitions per count: 1.
- **Main encoder** rests only at 11 (full-cycle detent), so two rest-to-rest
  moves per click. Transitions per count: 2.
- Both are sampled at 32 kHz from the TIM5 interrupt (`FastTick` in
  `Djin.cpp`) because the middle states can be well under a millisecond.

## Firmware behaviour (summary)

- SLEEP (power-on unless `kBootIntoEffect`), then MENU, then RUN. A tap on the
  menu switch moves SLEEP to MENU, MENU to RUN (enters the shown effect), RUN
  back to MENU. In RUN the menu encoder is **Length**, the Main encoder is
  effect specific, the footswitch toggles the effect on and off.
- LED: SLEEP breathes; MENU is off at base and flashes to 100% N times per 3 s;
  RUN is 40% (`kRunBase`) with the same flashes; bypassed is a dim steady 15%.
  The PWM is a 32 kHz interrupt with dithering. It must **not** run from the
  audio callback, which executes its samples back to back.
- Chain: input gain, mono conform, script, clean/blend, tape, compressor,
  limiter (-0.2 dB), volume.
- **Codec self-check:** all-zero input for 3 s pulses the AK4556 reset (PB11)
  and restarts the audio. The serial status line shows `rec=` (recoveries).
- Script One: slice length 25 steps (30 ms to 3 s, exponential, default step
  10) applied immediately by fading the playing slices out; Main encoder range
  +-18 sets direction mix, half-speed pitch-preserving slices and randomness.
  Tunables are named constants at the top of `script_one.h` and `Djin.cpp`.

## Known issues and open questions

1. **LED circuit short.** Measured: Seed pin 28 reads 3.2 V when driven high,
   and 0 V when set as a pulled-up input. So R4 is intact and the short is on
   the LED side of it: a dead LED, or a bridge to ground (the board had solder
   work done on it). Inspect the LED and its pads with the board unpowered. The
   Seed is not at risk (about 3 mA). Judge the LED behaviour only after this is
   fixed. The shorted circuit may also have been adding noise to the audio.
2. **Unexplained audio dropout.** Mid-session the codec input read exactly zero
   and the HardwareTest tone stopped, on several builds, then recovered with no
   firmware change. Leading theories: the codec stuck until a full power-cycle,
   or an intermittent connection (DC jack lead, Seed headers, input jack). The
   self-check covers the first. If it recurs, power-cycle the pedal (pull the
   9V) before anything else, and note whether that fixes it.
3. **1 kHz noise** was heard earlier. It was not in the digital stream (captured
   and analysed); it dropped with the ST-Link unplugged (ground loop). Worth
   re-listening once the LED short is fixed.
4. The mono detection thresholds (`kActive` 0.02, `kSilent` 0.004 in
   `djin_input.h`) now see the input after `kInputGain`, so they are effectively
   6x more sensitive than when written. Unverified by ear.

## Decisions beyond the PDF

- Footswitch = effect on/off (clean bypass). Menu encoder = effect select in
  the menu, Length in an effect. Tap in an effect returns to the menu (the PDF's
  "back to Sleep" is not reachable; Sleep is power-on only).
- Clean/Blend is clean (CCW) to processed (CW), mixed before tape, compressor
  and limiter; Volume is last.
- Script One: 25% of repeats re-grab the newest audio so it follows what you
  play; each buffer records 12% of the other channel and 30% of slices follow
  their partner voice (the "left and right interact" request); slices past +-13
  ease from half speed to 0.35x.
- No tape wow/flutter yet; the tape stage is saturation, a head-bump and a
  roll-off.

## Debugging recipes (what worked)

- **Read live state over SWD without halting.** Get addresses from
  `arm-none-eabi-nm -C build/Djin.elf` (`g_run`, `g_fx_on`, `g_volume`,
  `g_blend`, `g_cb_count`, `g_recoveries`, ...), then `read_memory` in an openocd
  Tcl script. The Seed's DMA buffers are `daisy::dsy_audio_rx_buffer` and
  `dsy_audio_tx_buffer`: an all-zero rx buffer means the codec is not
  delivering (a live input always shows noise).
- **Aliasing trap:** sampling a pin over the ST-Link locks onto a 1 kHz PWM and
  reads wrong duty. Add random jitter between reads, or read the firmware's own
  counters instead.
- **Spectrum check:** a temporary `volatile` capture buffer in the audio
  callback, `dump_image`'d over SWD, plus a short FFT on the Mac, found the input
  level problem and ruled out a 1 kHz component. (A write-only static is
  optimised away; make it `volatile`.)
- **Hold a pin for the meter:** `halt`, `mww 0x58020818 0x10` (sets PC4 via
  BSRR), `sleep`, `reset run`. Setting `MODER` / `PUPDR` the same way turns the
  pin into a pulled-up input.
- **Host-side tests:** compile `script_one.h` natively with a stub
  `daisy_core.h` (`#define DSY_SDRAM_BSS`) and `-fsanitize=address,undefined`.
  No bad indices or NaNs across rapid length and Main encoder changes.

## Next steps

1. Fix the LED short on the board; confirm the LED patterns.
2. Listen to Script One properly: the Main encoder, the Length range and
   response, bypass, and the volume and blend taper. Tune the constants.
3. Write Scripts 2-6 (the PDF only specifies Script One). The `Script`
   interface is in `djin_script.h`; the menu already offers six.
4. Decide a way back to Sleep, if one is wanted, and set `kBootIntoEffect` to
   `false` for the finished pedal.
5. Push the `libDaisy` `sunhaus-board` branch, then this repo.

## Loose ends on this machine

- `~/arm-gnu-toolchain-15.3.rel2` can be deleted (do not use it for Daisy).
- `~/arm-gnu-toolchain-12.2-dl/` holds the downloaded tarball and can be
  deleted; the working toolchain is `~/arm-gnu-toolchain-12.2.rel1`.
- Homebrew additions: `open-ocd` (and its `icu4c@79` dependency).
