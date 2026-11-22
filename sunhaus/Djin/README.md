# Djin, Skips and Fragments

Sunhaus pedal firmware for the Daisy Seed. Spec: `../Scripts 1 + global.pdf`.
Board support: `libDaisy/src/daisy_sunhaus.{h,cpp}`.

```bash
make -C ../../libDaisy && make -C ../../DaisySP   # once
make
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "program build/Djin.elf verify reset exit"
```

| File | What |
|---|---|
| `Djin.cpp` | main loop, SLEEP / RUN state machine, LED patterns, audio callback |
| `djin_fx.h` | shared chain: tape emulation, compressor, limiter (-0.2 dB) |
| `djin_input.h` | mono/stereo input conform |
| `djin_script.h` | `Script` interface and the pass-through placeholder |
| `script_one.h` | Script One, the cut-and-paste looper |

## Decisions beyond the PDF

- **Footswitch** toggles the effect on/off while a script runs. Off is a clean bypass and the LED dims to 15%.
- **Boot:** `kBootIntoEffect` (default `true`, for development) starts straight in Effect 1 so a reset never leaves the pedal silent. Set it to `false` for the spec's sleep-on-startup behaviour.
- **Menu switch:** SLEEP is the power-on state only. A tap in SLEEP opens the effect menu, a tap in the menu enters the shown effect, and a tap inside an effect fades out (0.5 s) and returns to the menu.
- **Menu encoder:** in the menu it picks the effect, 1-6 wrapping (a stand-in until the scripts exist). The LED is off in the menu and flashes to 100% N times every 3 s for effect N. Inside an effect it is the Length encoder, and the LED is lit at 40% (`kRunBase`) and flashes to 100% the same way. Set `kGrainFlash` to also flicker on every slice.
- **Clean/Blend:** clean (CCW) to fully processed (CW), mixed before the tape, compressor and limiter. Volume is last.
- **LED** is software PWM from the audio callback: D21 (PC4) has no timer channel.
- **Mono detection** is a level heuristic (one side playing, the other at the noise floor for 1 s). There is no 3 s pause on a change, just a 30 ms crossfade.
- **Script One slow slices** keep their pitch (granular time-stretch). Slices past |13| ease from 0.5x towards 0.35x speed.
- **Script One buffer interaction:** each buffer records 12% of the other channel, and 30% of slices grab near their partner voice on the other buffer.
- **Script One at position 0:** repeats the same slice, but 25% of repeats re-grab the newest audio so it keeps following what you play.
- **Scripts 2-6** are pass-through placeholders.
