# Boonta Effect Skeleton

A Boonta effect with everything but the effect. The board comes up, the controls
are read into a model, the LEDs and relays follow it, and audio passes straight
through. Writing the DSP is the only thing left to do.

The board's inputs, state, outputs and DSP live in separate files so that
building an effect means editing one of them, not untangling all four.

## Layout

| File | Role | Depends on |
|------|------|-----------|
| `PedalState.h/.cpp` | **Model** - what the pedal is set to | nothing |
| `Controls.h/.cpp` | **Controller** - inputs to model | libDaisy, model |
| `LedView.h/.cpp` | **View** - model to LEDs and relays | libDaisy, model |
| `Effect.h/.cpp` | **DSP** - model to samples | DaisySP, model |
| `NewEffect.cpp` | wiring only | all of the above |

The arrows only ever point one way. `Controls` writes the model and never reads
an LED; `LedView` and `Effect` read the model and never write it. Nothing but
`NewEffect.cpp` knows about the others' existence.

## Where to make changes

- **Build the effect** -> `Effect.cpp`. It is a pass-through: add state to
  `Effect.h`, resolve the model into coefficients at the top of `Process()`,
  and do the per-sample work in `ProcessChannel()`. It asks the model for
  `PARAM_DRIVE`, so it never has to care which knob that is, or whether the
  value came from a knob, the expression pedal, or a test harness.
- **Re-map a knob or switch** -> the tables at the top of `Controls.cpp`.
- **Change what the LEDs say** -> `LedView.cpp`.
- **Add a new parameter or mode** -> add it to `PedalState`, then set it in
  `Controls.cpp` and use it in `Effect.cpp`.

## Execution contexts

Two contexts, and the split matters:

- **Audio callback** (`AudioCallback` in `NewEffect.cpp`) runs
  `Controls::Process()` then `Effect::Process()`. `DaisyBoonta` initialises its
  `AnalogControl` smoothing filters with `AudioCallbackRate()`, so controls
  *must* be read once per callback or the knob smoothing is detuned.
- **Main loop** runs `LedView::Update()`. The LED driver transfer is I2C DMA and
  the relays are mechanical, so neither belongs in the callback. `LedView` only
  writes the relays when bypass actually changes, so they don't chatter.

The model is written from the callback and read from the main loop. Every member
is a naturally aligned word-sized POD written by exactly one context, so the
worst case for the reader is one frame of stale LED colour.

## What the controls carry

Every input is already read, debounced, smoothed and given a name in the model.
Nothing interprets those names yet - that is `Effect.cpp`'s job - so re-point
anything that doesn't suit the effect you are building.

| Control | Lands in the model as |
|---------|-----------------------|
| KNOB_1-6 | `PARAM_DRIVE`, `TONE`, `LEVEL`, `MIX`, `TIME`, `FEEDBACK`, each 0..1 |
| TOG_SW_1 | `TOGGLE_RANGE` - `POS_LOW` / `POS_MID` / `POS_HIGH` |
| TOG_SW_2 | `TOGGLE_CHARACTER` - same three positions |
| TOG_SW_3 | `TOGGLE_ROUTING` - same three positions |
| SW_SEL_1 / SW_SEL_2 | Previous / next preset, wrapping both ways |
| SW_FS_1 | Bypass - latching, drives the relays and footswitch LED 1 |
| SW_FS_2 | `AltHeld()` while down, and a tapped tempo in ms on each press |
| EXP | `GetExpression()`; unassigned by default (`kExpressionTarget`) |

The parameter names are a layer of indirection on purpose: six knobs called
`KNOB_1`..`KNOB_6` tell you nothing at the point of use, and the DSP should not
have to know its own wiring. If `PARAM_DRIVE` is the wrong name for what knob 1
does in your effect, rename it in `PedalState.h` rather than working around it.

## What the LEDs already do

Left over from the scaffold, and worth keeping until the effect has something
better to say:

- **Footswitch LED 1** - solid white while the effect is in circuit.
- **Footswitch LED 2** - flashes amber while the alt switch is held, otherwise
  blinks green at the tapped tempo.
- **Select LEDs 1-3** - one colour per preset, the selected one at full
  brightness and the rest dim.

The pedal **boots bypassed** (`PedalState::Reset()`), so the relays start out of
circuit and footswitch LED 1 starts dark. If nothing lights up at power-on, that
is why - press the left footswitch.

## Build

```
make
make program-dfu
```

## Bootloader

Nothing here is tied to an app type - the same sources link for all three.
Verified builds:

| `APP_TYPE` | Code lands in | Used |
|---|---|---|
| `BOOT_NONE` (default) | internal FLASH, 128K | 89628 B, 68% |
| `BOOT_SRAM` | SRAM, 480K | 89468 B, 18% |
| `BOOT_QSPI` | QSPI, 7936K | 89468 B, 1% |

68% of internal flash is gone before you write a line of DSP, and almost none of
that is the effect - a pass-through `Effect.cpp` is within 100 bytes of the
worked drive/tone/level chain this skeleton was cut from. The cost is libDaisy's
board support, so it is a floor, not a starting point you can optimise away. A
real effect will likely need the bootloader. Set `APP_TYPE` in the `Makefile`
(there is a commented-out line ready), then:

```
make program-boot
```

once to install the bootloader, and `make program-dfu` from then on with the
board in bootloader mode. Two consequences of switching:

- `make program` (openocd) is refused for boot app types - DFU only. The
  `.vscode` debug configuration goes with it.
- `BOOT_QSPI` executes from QSPI, which fetches slower than internal flash.
  Prefer `BOOT_SRAM` for anything with a real DSP load.

One number to watch either way: libDaisy's DMA buffers sit in `RAM_D2_DMA`,
which the boot linker scripts shrink from 288K to 32K. The LED driver and codec
buffers already use 17096 B of that 32K, so leave room if you add DMA
peripherals.
