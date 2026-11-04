// # Boonta Beads
//
// A granular processor in the shape of Mutable Instruments' Beads, built on
// the Boonta template's split along MVC lines:
//
//   PedalState  (model)      what the pedal is set to. No hardware.
//   Controls    (controller) knobs/switches/expression -> model.
//   MidiControl (controller) USB and DIN MIDI -> the same model.
//   LedView     (view)       model -> LEDs and bypass relays.
//   Effect      (dsp)        model -> samples. The grain engine.
//   Beads.cpp                wiring, and nothing else.
//
// ## Controls
// | Control  | Function                                                      |
// |----------|---------------------------------------------------------------|
// | KNOB_1   | Time: how far back grains are drawn from, 20 ms to 20 s        |
// | KNOB_2   | Size: grain length, 10 ms to 1 s                               |
// | KNOB_3   | Pitch: grain transposition, -2 to +2 octaves                   |
// | KNOB_4   | Density: grain rate, and how clocked it is                     |
// | KNOB_5   | Shape: grain envelope, percussive / triangle / reversed        |
// | KNOB_6   | Mix: dry to wet                                                |
// | TOG_SW_1 | Seed: per-grain scatter of pitch, pan and start point          |
// | TOG_SW_2 | Feedback: off / 0.45 / 0.85 back into the buffer                |
// | TOG_SW_3 | Quality: clean / warm / lo-fi (8 bit, quarter rate)            |
// | SW_SEL_1 | Previous pitch quantisation                                    |
// | SW_SEL_2 | Next pitch quantisation (free / chromatic / harmonic)          |
// | SW_FS_1  | Freeze: hold for momentary, hold past 700 ms to latch          |
// | SW_FS_2  | Bypass (latching, drives the relays)                           |
// | EXP      | Pitch, once moved                                              |
// | MIDI     | Everything above; CC 20-31, see MidiMap.h                      |

#include "daisy_boonta.h"

#include "Controls.h"
#include "Effect.h"
#include "LedView.h"
#include "MidiControl.h"
#include "PedalState.h"
#include "UsbDiag.h"

using namespace daisy;

static DaisyBoonta hw;

static PedalState  state;
static Controls    controls;
static MidiControl midi;
static LedView     view;
static Effect      effect;

#ifdef PROFILE_CPU
#include "util/CpuLoadMeter.h"

/** Audio callback timing, for a debugger to read. The pedal has no screen and
 *  five LEDs already spoken for, so the only way to get a number off the
 *  board is to leave it somewhere an ST-Link can see it. Uncomment the
 *  C_DEFS line in the Makefile to build this in; see the README for the gdb
 *  one-liner.
 *
 *  Volatile because nothing in the firmware ever reads these back, and the
 *  optimiser is entitled to notice that. */
static CpuLoadMeter load_meter;

static struct
{
    volatile uint32_t blocks; /**< audio callbacks since boot */
    volatile float    avg;    /**< smoothed load, 0 to 1      */
    volatile float    min;
    volatile float    max; /**< worst block since boot     */
    volatile float    sample_rate;
    volatile uint32_t block_size;
    volatile uint32_t midi_usb; /**< messages in, per transport */
    volatile uint32_t midi_uart;
    volatile uint32_t midi_accepted;
} profile;

/** Clear the worst-block figure without rebooting, so a measurement can be
 *  scoped to one setting rather than to everything since power-on. Call it
 *  from gdb: `set var profile_reset = 1`. */
volatile uint32_t profile_reset = 0;
#endif

// Audio-callback context: read the inputs, then render the audio. All of it is
// deterministic and allocation free. The LEDs and relays are deliberately not
// touched here -- see the main loop.
//
// Controls runs before MidiControl so that the remote has the last word within
// a block; which of them keeps a parameter over time is settled by the pickup
// rule in Controls, not by this ordering.
//
// The two values that travel back out of the DSP are the block's output peak
// and the level the recording path saw, which the view uses as a meter and a
// clip indicator. Writing them here rather than from inside Effect keeps the
// dependency visible: the DSP still only reads the model.
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
#ifdef PROFILE_CPU
    load_meter.OnBlockStart();
#endif

    controls.Process();
    midi.Process();

    float in_peak = 0.f;
    state.SetOutputLevel(effect.Process(state, in, out, size, &in_peak));
    state.SetInputPeak(in_peak);

#ifdef PROFILE_CPU
    load_meter.OnBlockEnd();

    if(profile_reset)
    {
        profile_reset = 0;
        load_meter.Init(profile.sample_rate, profile.block_size);
        profile.blocks = 0;
    }

    profile.blocks++;
    profile.avg           = load_meter.GetAvgCpuLoad();
    profile.min           = load_meter.GetMinCpuLoad();
    profile.max           = load_meter.GetMaxCpuLoad();
    profile.midi_usb      = midi.ReceivedUsb();
    profile.midi_uart     = midi.ReceivedUart();
    profile.midi_accepted = midi.Accepted();
#endif
}

int main(void)
{
    hw.Init();

    // Trim HSI48 before USB comes up. libDaisy points the USB clock at a
    // free-running RC oscillator and never locks it to anything; the ROM DFU
    // bootloader, which enumerates on this board where the app does not, does
    // lock it. See UsbDiag.h.
    usbdiag::EnableCrs();

    // MIDI first. USB device enumeration is time-critical -- the host starts
    // asking for descriptors as soon as the port is live -- and Effect::Init
    // clears four megabytes of SDRAM.
    midi.Init(&hw, &state);

    controls.Init(&hw, &state);
    view.Init(&hw);
    effect.Init(hw.AudioSampleRate());

#ifdef PROFILE_CPU
    profile.sample_rate = hw.AudioSampleRate();
    profile.block_size  = hw.AudioBlockSize();
    load_meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
#endif

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    // Main-loop context: anything slow, blocking or mechanical. The LED driver
    // transfer is I2C DMA and the relays are physical switches, so neither
    // belongs in the audio callback.
    while(1)
    {
        view.Update(state);

        // Undo the PHY clock gating libDaisy's suspend callback leaves behind;
        // without this the USB data path stays dead after any host suspend.
        usbdiag::Service();

        System::Delay(1);
    }
}
