# Boonta -> DaisyDuino port

Porting `boonta/NewEffect` to the Arduino IDE. This file is the hardware map
the port needs, and a record of what is done and what is not.

## Status

**All three app types build.** `./build.sh`, `./build.sh BOOT_SRAM`,
`./build.sh BOOT_QSPI`.

| Piece | State |
|---|---|
| `PedalState.h/.cpp` | Shared, byte-identical with `NewEffect` |
| `Effect.h/.cpp` | Shared, byte-identical with `NewEffect` |
| `BoontaDuino.h/.cpp` | New - the board layer DaisyDuino doesn't have |
| `Controls.h/.cpp` | Ported - board type and `millis()` only |
| `LedView.h/.cpp` | Ported - board type and `millis()` only |
| `NewEffectDuino.ino` | Done - `setup()`/`loop()` in place of `main()` |
| `boot_sram.ld`, `boot_qspi.ld` | New - the bootloader layouts, see below |
| `BootApp.cpp` | New - the SystemInit override the bootloader needs |
| `build.sh`, `flash.sh`, `sram-load.cfg` | New - what the Makefile used to do |
| `../QspiWriter` | New - the only working way into QSPI on this board |

**`BOOT_SRAM` and `BOOT_QSPI` both run on the pedal**, with audio flowing --
see **What ran on hardware** below. `BOOT_NONE` has not been on the board,
since flashing it would overwrite the bootloader the other two need.

`BOOT_QSPI` took a detour. DFU is the documented route into QSPI and it does
not work on this board at all, so there is now a flash loader --
`../QspiWriter` -- that writes the chip from the target side over the ST-Link.
See **Getting into QSPI**.

Nor has anyone looked at it. The knob order, the scrambled select-LED channels
and the toggle polarity are all reproduced from reading `daisy_boonta.cpp`, and
firmware running is not the same as the right LED lighting up.

All four shared files are byte-identical to `boonta/NewEffect`'s, verified with
`diff`. `PedalState` and `Effect` also compile for an x86 host with
`clang++ -std=c++14 -Wall -Wextra` against nothing but `DaisySP/Source`. That is
the MVC split paying off: half the project ported for free, and the half that
didn't is the half that was always going to.

Keeping them identical cost one thing: `Effect.cpp`'s DaisySP include is now
guarded with `#ifdef ARDUINO`, because DaisyDuino bundles DaisySP at a path of
its own. The same guard is in `NewEffect/Effect.cpp`, which still builds at
89628 B / 68.38% exactly as before.

## Toolchain

The Arduino IDE is installed, and its bundled `arduino-cli` (1.5.1) is at:

```
/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe
```

STM32 core 2.11.0 is installed and **already knows the Daisy Seed** - it is
`GenH7.menu.pnum.DAISY_SEED` in the core's `boards.txt`, building as
`STM32H750xx` with its own `DAISY_SEED.ld`. FQBN:

```
STMicroelectronics:stm32:GenH7:pnum=DAISY_SEED
```

DaisyDuino 1.7.0 (Electrosmith) is installed. Build with `./build.sh`, which
wraps that FQBN and the build properties the bootloader layouts need; see
**Bootloader** below.

It turned out to carry more than expected. `src/utility/` has libDaisy's
`Switch` and `AnalogControl` and a `LedDriverPca9685` with the same API as the
libDaisy one, reimplemented over `Wire`. So the debouncing, the knob smoothing
and the LED driver all came across rather than being hand-rolled, which is why
`Controls.cpp` and `LedView.cpp` differ from their libDaisy versions only in
the board type and `System::GetNow()` -> `millis()`.

The one gap is **`Switch3`**, which is not bundled. The three-way toggles are
two pulled-up pins read together, reimplemented in
`BoontaDuino::GetSwitchPosition()` to match `daisy::Switch3::Read()` exactly.

## Bootloader

All three of libDaisy's app types build. `arduino-cli` has no `APP_TYPE`, so
`build.sh` passes the same three decisions in as build properties:

```
./build.sh              # BOOT_NONE -- internal flash, the default
./build.sh BOOT_SRAM    # copied into AXI SRAM by the bootloader
./build.sh BOOT_QSPI    # executed in place from QSPI
```

| App type | Runs from | Image | of region | `.data`+`.bss` | of region |
|---|---|---|---|---|---|
| `BOOT_NONE` | flash `0x08000000` | 55996 B | 43% of 128K | 4332 B in RAM_D1 | 1% of 512K |
| `BOOT_SRAM` | AXI SRAM `0x24000000` | 55780 B | 11% of 480K | 4332 B in DTCMRAM | 3% of 128K |
| `BOOT_QSPI` | QSPI `0x90040000` | 55780 B | 1% of 7936K | 4332 B in RAM_D1 | 1% of 512K |

DaisyDuino's SAI buffers add 19504 B in RAM_D2 whichever type you build; they
are in `.sram1_bss`, which none of the three scripts moves, because D2 is the
only one of those regions DMA can reach.

### Against the libDaisy build

| Build | Image | Of 128K internal flash |
|---|---|---|
| libDaisy `NewEffect` | 89628 B | 68% |
| DaisyDuino `NewEffectDuino` (`BOOT_NONE`) | 55996 B | 43% |

This was expected to be the thing that killed the port: libDaisy spends 68% of
internal flash before any DSP, so having no bootloader looked fatal. It runs
the other way. The Arduino build is 33 KB smaller -- DaisyDuino's board support
is leaner than a full `DaisySeed::Init()`, with no USB stack, no MIDI UART and
no FatFS -- so of the two it is the libDaisy build that needs the bootloader
first. Which is not a reason to skip it here: 43% before a line of DSP is still
most of a 128K part, and both are now available.

Three pieces had to be built, because DaisyDuino has none of them.

**1. The layouts.** `boot_sram.ld` and `boot_qspi.ld`, derived from the STM32
core's `DAISY_SEED.ld` exactly as libDaisy derives its two from
`STM32H750IB_flash.lds`. Each moves `.isr_vector`, `.text` and `.rodata` out of
internal flash, which is all the bootloader asks for -- it will only run a
program whose vectors land in SRAM or QSPI.

The SRAM one also moves `.data` and `.bss` to DTCMRAM. That is not tidiness:
the bootloader keeps the top 32K of AXI SRAM for itself, so an SRAM image can
be at most 480K, and leaving RAM in AXI SRAM would eat into the image. The cost
is that an SRAM app's RAM budget is DTCMRAM's 128K rather than 512K. The QSPI
one leaves RAM exactly where the stock script put it.

**2. Getting them used.** The core's link recipe is

```
-Wl,--default-script={build.variant.path}/{build.ldscript} ... {compiler.c.elf.extra_flags}
```

`compiler.c.elf.extra_flags` is substituted *after* the variant's script, and
the last `--default-script` on the command line wins, so passing another one
there replaces `DAISY_SEED.ld` without touching the installed core. Verified
rather than assumed -- `objdump -h` puts `.isr_vector` at `0x24000000` and
`0x90040000` in the two boot builds.

**3. SystemInit.** The core's `Reset_Handler` calls `SystemInit()` before
`main()`, and the core's `SystemInit()` does two things that are fatal to a
bootloaded app: it resets the whole RCC tree to HSI with every PLL off, and it
writes `SCB->VTOR = FLASH_BANK1_BASE`. Under `BOOT_QSPI` the first of those
pulls the QSPI kernel clock out from under a CPU that is fetching instructions
through it.

libDaisy's answer is `-DBOOT_APP`, which `#ifdef`s the call out of its own
`startup_stm32h750xx.c`. The Arduino core's startup is a fixed `.s` file in the
installed core, so instead the call is redirected at link time with
`-Wl,--wrap=SystemInit` to the stub in `BootApp.cpp`, which keeps the FPU
enable, points VTOR at `_svector_table` (a symbol the two scripts place at the
start of `.isr_vector`, so the address lives in one place) and leaves RCC
alone. `DAISY.init()` configures the clocks properly a moment later.

Also verified: in both boot builds `Reset_Handler` calls `__wrap_SystemInit`,
and the real `SystemInit` is gone from the image entirely.

### Flashing

```
./flash.sh boot         # Daisy bootloader -> internal flash, over the ST-Link
./flash.sh BOOT_NONE    # app -> internal flash, over the ST-Link
./flash.sh BOOT_SRAM    # app -> QSPI 0x90040000, over the ST-Link
./flash.sh BOOT_QSPI    # app -> QSPI 0x90040000, over the ST-Link
./flash.sh sram         # app -> straight into SRAM, over the ST-Link
```

Everything goes over the ST-Link, and nothing needs a button held. That is not
the usual Daisy arrangement -- QSPI is normally written over DFU -- and the
reason is **Getting into QSPI**, below. `USE_DFU=1 ./flash.sh BOOT_QSPI` takes
the DFU route instead, on a board where it works.

`BOOT_SRAM` and `BOOT_QSPI` images go to the same address; the bootloader
checks the image before jumping and runs it where its vectors point.

`./flash.sh sram` is the development loop: about a second, no flash wear, gone
on the next power cycle. `sram-load.cfg` is lifted from
`boonta/MultiEffect/sram-load.cfg` -- the hazard it works around is in the
bootloader's handover, not in the app, so it applies here unchanged.

Note that `./flash.sh BOOT_NONE` overwrites the bootloader if one is installed,
and `./flash.sh boot` puts it back.

As of 2026-11-03 the board has the `intdfu` bootloader in internal flash and
this project's `BOOT_QSPI` build in QSPI, so `./flash.sh sram` works with no
further setup. It held MultiEffect's `BOOT_SRAM` build in QSPI until the
`BOOT_QSPI` test overwrote it; that image is gone and would have to be rebuilt
and rewritten with `../QspiWriter/qspi-write.sh build/MultiEffect.bin`. Before
any of this it held `boonta/NewEffect` in internal flash and no bootloader at
all, which is why `./flash.sh boot` had to run first.

### What ran on hardware

`BOOT_SRAM`, loaded with `./flash.sh sram` on 2026-11-02 and read back over the
ST-Link:

- `SCB->VTOR` = `0x24000000`. This is the one that matters. The core's real
  `SystemInit()` would have written `0x08000000` over the value the bootloader
  left, so VTOR still pointing at the app's own vectors is proof that
  `-Wl,--wrap=SystemInit` took and `BootApp.cpp` ran in its place.
- `CPACR` = `0x00f00000`: the FPU is on.
- PC sampled at `0x24003c56`, `0x2400b43c`, `0x24004ace`, `0x24004acc`,
  `0x24003c66`, `0x24004c72` -- spread through the image in SRAM, not stuck.
- `dsy_audio_rx_buffer` in RAM_D2 changes on every read, with values that look
  like 24-bit samples near zero, and `dsy_audio_tx_buffer` moves with it. The
  codec is clocking, the SAI DMA is running and the audio callback is being
  served.
- `PedalState` at `0x20000178` -- in DTCMRAM, where only the SRAM script puts
  it. Six knobs read plausible positions, the expression input dithers between
  reads, `preset_` is 0 and `tap_interval_ms_` is 500.0, the clean defaults.
  (The libDaisy build caught a spurious edge on each of two switches at
  startup. This one did not.)

So the SRAM layout, the linker-script override and the `SystemInit` wrap are
confirmed working together, on a build whose `.data` and `.bss` live in DTCMRAM
and whose DMA buffers live in D2.

`BOOT_QSPI`, written into QSPI with `./flash.sh BOOT_QSPI` on 2026-11-03 and
read back after a reset:

- PC sampled at `0x90043c52`, `0x90044acc`, `0x90043fce`, `0x90044ad6`,
  `0x90043c66`, `0x90041e04` -- every one of them inside the QSPI chip. The
  core is fetching instructions over the QSPI bus. That is what execute in
  place means and it is the whole difference between this app type and the
  other two.
- `SCB->VTOR` = `0x90040000`: the vector table is in QSPI too, and interrupts
  are being taken through it -- four of those six samples were in
  `Handler External Interrupt(12)`, the audio DMA.
- `RCC_AHB3ENR` = `0x00005000`, so QSPIEN is set. DaisyDuino never touches the
  QSPI peripheral, which is exactly what this build needs: the memory mapping
  the bootloader set up is left alone.
- MSP `0x2407ff70` -- the top of AXI SRAM, where `boot_qspi.ld` puts it, rather
  than the top of DTCMRAM where the SRAM build's is.
- `dsy_audio_rx_buffer` and `dsy_audio_tx_buffer` move between reads, and
  `PedalState` -- at `0x24000178` in AXI SRAM this time, since only the SRAM
  build moves `.bss` to DTCMRAM -- reads the same plausible knobs,
  `preset_` 0 and `tap_interval_ms_` 500.0.

So `boot_qspi.ld` is confirmed too, and so is the `SystemInit` wrap on the path
that needed it most: the core's real `SystemInit()` resets RCC to HSI, which
would have stopped the QSPI clock out from under the instruction stream.

What none of this shows is that the pin map is right. Nobody has looked at the
pedal. Audio flowing says the codec and the callback work; it says nothing
about which knob moves which parameter or which LED lights.

### Getting into QSPI

Neither documented route works on this board, which is why there is now a
third one.

DFU is the documented way into QSPI, and the Daisy bootloader's DFU window
never opens here. With the Daisy's USB connected and both bootloader variants
flashed in turn (`intdfu` and `extdfu`), Windows' device list was polled about
sixty times across a reset -- quarter-second granularity over fifteen seconds,
covering the whole 2.5s grace period twice over. No device of any kind
appeared. That is not a timing problem and not a cable problem:

- Jumped into the STM32's own ROM bootloader over SWD, the same board
  enumerates immediately as `[0483:df11]`, "DFU in FS Mode". The connector,
  the cable, the port and VBUS are all fine.
- That ROM DFU offers exactly two alt settings: `@Internal Flash` at
  `0x08000000` and `@Option Bytes`. No QSPI. It cannot put anything at
  `0x90040000`.
- `boonta/MultiEffect/HANDOVER.md` already records that USB MIDI never
  enumerates on this board, and that libDaisy's own `seed/USB_MIDI` example
  fails identically on the same cable. The Daisy bootloader is built on that
  same USB stack, so it fails the same way.

The pedal's USB works; libDaisy's USB stack does not. That closes the DFU route
to QSPI until the stack is fixed, and it has nothing to do with this port.

#### The openocd route, which does not work either

openocd has an `stmqspi` driver that can program a memory-mapped QSPI chip over
SWD. It does not work here, but two things came out of trying:

- **The bootloader gates QSPI off before handing over.** At the handover
  breakpoint `RCC_AHB3ENR` is `0x00000000` and `0x90040000` is unreadable.
  Sampling across a boot shows QSPI live throughout the bootloader's window,
  off for roughly 150ms across the jump, then back on once the resident app
  reaches `DaisySeed::Init()`. So the moment to catch a configured QSPI is
  while an app is running, not at the handover -- which is the opposite of
  where `sram-load.cfg` takes control.
- **`flash probe` drops the SWD link.** "jtag status contains invalid mode
  value - communication failure", with QUADSPI demonstrably live
  (`CR=0x01400001`, `CCR=0x1f18edeb`). `stmqspi` is not usable over ST-Link's
  HLA transport.

#### The route that does work

`../QspiWriter`: the thing every real flash loader is, a small program running
from SRAM that does the writing, driven over the ST-Link by a debugger that
knows nothing about the flash. It is loaded the way `./flash.sh sram` loads an
app, and fed the payload 32K at a time through a mailbox, stopping at a
one-instruction function the host keeps a hardware breakpoint on. Each chunk is
verified against the memory-mapped chip as it goes. `../QspiWriter/README.md`
has the details.

Two things had to change on this side for it:

- `./flash.sh BOOT_SRAM` and `BOOT_QSPI` now call it instead of `dfu-util`.
  `USE_DFU=1` still takes the DFU route.
- `sram-load.cfg` had to learn where the bootloader is really going to jump.
  It used to read the reset vector at `0x24000004`, which is the image
  *openocd* last loaded, not the one the bootloader is about to load over the
  top of it. That was harmless while every image in QSPI was a `BOOT_SRAM`
  one and openocd only ever loaded builds of the same program; it stops being
  harmless the moment a `BOOT_QSPI` image is resident. It now reads
  `0x90040004` instead -- the QSPI image's own reset vector, which is what the
  bootloader actually loads into PC -- after spending a reset finding a moment
  when QSPI is mapped at all. `../QspiWriter/README.md`, **Finding the
  handover**, explains why that is awkward.

#### If openocd stops being able to examine the target

Repeated `flash probe` attempts left it unexaminable, every connect reporting
"target stm32h7x.cpu0 examination failed". What recovers it:

```
openocd -f interface/stlink.cfg -c "adapter speed 480" \
        -f target/stm32h7x.cfg \
        -c "reset_config srst_only srst_nogate connect_assert_srst" \
        -c "catch {init}; catch {reset halt}; catch {reset halt}; ..."
```

Both halves matter -- the slower adapter clock and connecting with SRST
asserted -- and the first `reset halt` often still fails, so retry it a few
times before believing a failure. That last point is the same lesson
`HANDOVER.md` records: check openocd is alive before trusting a negative
result.

#### If the pedal looks dead

Check whether it is halted before assuming it crashed. An openocd command that
halts the target and then errors before its `resume` leaves the core halted
with `C_HALT` set, and openocd's `shutdown` does not undo that. The pedal goes
silent, the LEDs freeze, and nothing recovers it except a power cycle or
another debugger connection -- which looks exactly like a crash and is not one.
It happened here from a mistyped `echo` in a one-off `-c` chain, and the board
sat halted for an hour.

The two tell it apart in one connect. Put this in a `.cfg` and pass it with
`-f`, rather than fighting the quoting in a `-c` chain -- which is how the
problem got made in the first place:

```tcl
init
halt
echo [format "CFSR %08x" [mrw 0xE000ED28]]
echo [format "HFSR %08x" [mrw 0xE000ED2C]]
echo [format "DHCSR %08x" [mrw 0xE000EDF0]]
resume
```

A crash leaves `CFSR` or `HFSR` non-zero. A debugger-halted core leaves both at
zero with the program counter sitting wherever it was last halted -- compare it
against the last PC the previous openocd run printed, and if they match to the
stack pointer as well, nothing has executed in between. `resume` is the whole
fix.

Do not read the halt reason out of `DFSR` (`0xE000ED30`) without clearing it
first. Its bits are sticky and write-1-to-clear, so they accumulate across
every session since the last reset; a `DFSR` of `0x0b` says only that a
breakpoint, a vector catch and a halt have all happened at some point, which
is why openocd will cheerfully report "halted due to breakpoint" when the
breakpoint was yesterday's. Write `0x1f` to it, halt again, and read it then.

### Still only reasoning

`BOOT_NONE` has not been flashed, since doing so would overwrite the bootloader
the other two need.

One thing about the QSPI build is still argued rather than measured:
`QSPIHandle`'s `.qspiflash_text` / `_data` / `_bss` sections have nowhere to go
in `boot_qspi.ld`. Nothing references them today so nothing is emitted, but
anything that starts using `QSPIHandle` in a QSPI build will need those
sections placed somewhere that is not QSPI. libDaisy's own `qspi.lds` has them
commented out, so there is no prior art to copy.

(The other one -- that `ConfigureClocks()` reconfigures the PLLs while the code
is executing out of QSPI -- is now settled. It does, and the app runs anyway,
because DaisyDuino never re-initialises the QSPI peripheral and the
bootloader's memory mapping survives.)

## Pin map

Taken from `libDaisy/src/daisy_boonta.cpp` and translated through the Daisy Seed
pin table in `libDaisy/src/daisy_seed.h`. The Arduino variant is the generic
`H742I..._H750IB...` one, so it knows STM32 port names, not Daisy's `D0`-`D30`.

| Function | Daisy | STM32 |
|---|---|---|
| TOG_SW_1 A / B | D1 / D2 | PC11 / PC10 |
| TOG_SW_2 A / B | D3 / D4 | PC9 / PC8 |
| TOG_SW_3 A / B | D5 / D6 | PD2 / PC12 |
| SW_SEL_1 | D7 | PG10 |
| SW_SEL_2 | D8 | PG11 |
| SW_FS_1 | D9 | PB4 |
| SW_FS_2 | D10 | PB5 |
| I2C1 SCL / SDA | D11 / D12 | PB8 / PB9 |
| MIDI out / in | D13 / D14 | PB6 / PB7 |
| Expression | D15 | PC0 |
| Pot 1..6 | D16..D21 | PA3, PB1, PA7, PA6, PC1, PC4 |
| Relay out | D22 | PA5 |
| Relay bypass | D23 | PA4 |
| Relay in | D24 | PA1 |

Each three-way toggle is two pins read together, not one analog pin.

## Three things that will silently break the port

**1. The knob order is reversed.** `InitAnalogControls()` assigns ADC channels
back to front:

```cpp
cfg[KNOB_1].InitSingle(PIN_KNOB_6);   // ADC channel 0 reads D21
...
cfg[KNOB_6].InitSingle(PIN_KNOB_1);   // ADC channel 5 reads D16
```

So `GetKnobValue(KNOB_1)` reads **D21 / PC4**, not D16. Whether the pots are
wired backwards on the PCB and this corrects it, or it is a bug nobody noticed
because the mapping table in `Controls.cpp` hides it, the Arduino port has to
reproduce it or every knob moves the wrong parameter.

| Enum | Actually reads |
|---|---|
| `KNOB_1` | D21 / PC4 |
| `KNOB_2` | D20 / PC1 |
| `KNOB_3` | D19 / PA6 |
| `KNOB_4` | D18 / PA7 |
| `KNOB_5` | D17 / PB1 |
| `KNOB_6` | D16 / PA3 |

**2. The select LEDs are not in order on the driver.** All 15 LED channels are
on one PCA9685, and the select triples are scrambled:

| LED | PCA9685 channels (R, G, B) |
|---|---|
| `FOOTSWITCH_LED_1` | 0, 1, 2 |
| `SELECT_LED_1` | 3, 4, 5 |
| `SELECT_LED_3` | 6, 7, 8 |
| `SELECT_LED_2` | 9, 10, 11 |
| `FOOTSWITCH_LED_2` | 12, 13, 14 |

Select LEDs 2 and 3 are swapped relative to channel order. The `LedOrder` enum
declares select LEDs as R, B, G while `SetSelectLed()` passes `g` to the `_B`
constant and `b` to the `_G` one, so those two inversions cancel and the
channels do end up consecutive R, G, B - but only because of a double negative.
Port the table above, not the enum.

**3. The sample rate is 48014 Hz, not 48000.** `daisy_boonta.cpp` defines
`SAMPLE_RATE 48014.f`. Anything tuned by ear against the libDaisy build will be
very slightly off if the Arduino build runs at exactly 48000.

## LED driver

PCA9685, one chip, 7-bit I2C address **0x40** (`PCA9685_I2C_BASE_ADDRESS`
`0b01000000` OR'd with the configured `0x00`), on I2C1 at 1 MHz.

libDaisy drives it with DMA double-buffering and
`led_driver_.SwapBuffersAndTransmit()`. Arduino's `Wire` is blocking, so the
transfer happens inline in the main loop instead. That is survivable precisely
because `LedView::Update()` was already main-loop-only and never ran in the
audio callback - but 15 channels at 1 MHz is not free, and the loop rate will
drop. Measure it before assuming the LED refresh still looks smooth.

## Not carried over

- **MIDI** (D13/D14) - `NewEffect` does not use it.
- **Relay debouncing** - there is none to carry; `LedView` only writes the
  relays when bypass actually changes, which is all they need.
