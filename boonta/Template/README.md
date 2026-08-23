# Boonta Template

Starting point for a Boonta effect. The board's inputs, state, outputs and DSP
live in separate files so that building an effect means editing one of them, not
untangling all four.

## Layout

| File | Role | Depends on |
|------|------|-----------|
| `PedalState.h/.cpp` | **Model** - what the pedal is set to | nothing |
| `Controls.h/.cpp` | **Controller** - inputs to model | libDaisy, model |
| `LedView.h/.cpp` | **View** - model to LEDs and relays | libDaisy, model |
| `Effect.h/.cpp` | **DSP** - model to samples | DaisySP, model |
| `Template.cpp` | wiring only | all of the above |

The arrows only ever point one way. `Controls` writes the model and never reads
an LED; `LedView` and `Effect` read the model and never write it. Nothing but
`Template.cpp` knows about the others' existence.

## Where to make changes

- **Build an effect** -> `Effect.cpp`. It asks the model for `PARAM_DRIVE`, so it
  never has to care which knob that is, or whether the value came from a knob,
  the expression pedal, or a test harness.
- **Re-map a knob or switch** -> the tables at the top of `Controls.cpp`.
- **Change what the LEDs say** -> `LedView.cpp`.
- **Add a new parameter or mode** -> add it to `PedalState`, then set it in
  `Controls.cpp` and use it in `Effect.cpp`.

## Execution contexts

Two contexts, and the split matters:

- **Audio callback** (`AudioCallback` in `Template.cpp`) runs `Controls::Process()`
  then `Effect::Process()`. `DaisyBoonta` initialises its `AnalogControl`
  smoothing filters with `AudioCallbackRate()`, so controls *must* be read once
  per callback or the knob smoothing is detuned.
- **Main loop** runs `LedView::Update()`. The LED driver transfer is I2C DMA and
  the relays are mechanical, so neither belongs in the callback. `LedView` only
  writes the relays when bypass actually changes, so they don't chatter.

The model is written from the callback and read from the main loop. Every member
is a naturally aligned word-sized POD written by exactly one context, so the
worst case for the reader is one frame of stale LED colour.

## Stock behaviour

A two-channel drive -> tone -> level chain, so there is something audible to
verify wiring against.

| Control | Function |
|---------|----------|
| KNOB_1-6 | Drive, Tone, Level, Mix, Time, Feedback |
| TOG_SW_1 | Range: down = low gain, centre = mid, up = high |
| TOG_SW_2 | Character (unused by the stock effect) |
| TOG_SW_3 | Routing (unused by the stock effect) |
| SW_SEL_1 / SW_SEL_2 | Previous / next preset (shown on the select LEDs) |
| SW_FS_1 | Bypass - latching, drives the relays and footswitch LED 1 |
| SW_FS_2 | Alt - momentary gain boost, and taps a tempo on footswitch LED 2 |
| EXP | Read into the model; unassigned by default (`kExpressionTarget`) |

`TIME`, `FEEDBACK`, `CHARACTER`, `ROUTING` and the preset index are plumbed
through the model but unused by the stock effect - they are there so a real
effect has somewhere to land.

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
| `BOOT_NONE` (default) | internal FLASH, 128K | 89720 B, 68% |
| `BOOT_SRAM` | SRAM, 480K | 89552 B, 18% |
| `BOOT_QSPI` | QSPI, 7936K | 89552 B, 1% |

68% of internal flash is already gone before you write any DSP, so a real effect
will likely need the bootloader. Set `APP_TYPE` in the `Makefile` (there is a
commented-out line ready), then:

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
