// # Boonta MultiEffect
//
// Three effects -- EQ, drive, reverb -- each with its own page of six knobs,
// reorderable in the chain, plus a performance page for everything global.
// Built on the Boonta Template's MVC split:
//
//   PedalState  (model)      what the pedal is set to. No hardware.
//   Controls    (controller) knobs/switches/expression -> model.
//   LedView     (view)       model -> LEDs and bypass relays.
//   Chain       (dsp)        model -> samples, via EqEffect/DriveEffect/ReverbEffect.
//   MultiEffect.cpp          wiring, and nothing else.
//
// Six pots serve four pages, so a pot almost never matches the value the page
// it just landed on is holding. Controls parks each knob until the pot sweeps
// through the stored value; the page footswitch LED breathes until something on
// the page has actually been changed. See Controls.cpp.
//
// Settings live in QSPI flash and come back at power-on, so the knobs are parked
// at boot too -- the saved values win, not the pots. See Storage.cpp.
//
// LEDs: the page footswitch shows the current page's colour -- teal EQ, gold
// drive, purple reverb, white meta. The bypass footswitch is green in circuit
// and red out. The three select LEDs are the chain, one per position, coloured
// by the effect there and dimmed to a fifth when that effect is switched out.
//
// ## Controls
// | Control  | Function                                                     |
// |----------|--------------------------------------------------------------|
// | KNOB_1-6 | The six parameters of the current page (see below)           |
// | TOG_SW_1 | Drive range: down = low gain, centre = mid, up = high        |
// | TOG_SW_2 | Reverb size: down = room, centre = plate, up = cavern        |
// | TOG_SW_3 | EQ band width: down = wide, centre = medium, up = tight      |
// | SW_SEL_1 | Previous chain order                                         |
// | SW_SEL_2 | Next chain order                                             |
// | SW_FS_1  | Short: bypass this page's effect. Long: next page            |
// | SW_FS_2  | Master bypass (latching, drives the relays)                  |
// | EXP      | Read into the model; unassigned by default                   |
//
// ## Pages
// |         | KNOB_1   | KNOB_2  | KNOB_3   | KNOB_4    | KNOB_5   | KNOB_6   |
// |---------|----------|---------|----------|-----------|----------|----------|
// | EQ      | Low freq | Mid freq| High freq| Low gain  | Mid gain | High gain|
// | Drive   | Gain     | Tone    | Character| Bias      | Level    | Mix      |
// | Reverb  | Time     | Damping | Pre-delay| Diffusion | Low cut  | Mix      |
// | Meta    | EQ amt   | Rvb amt | In gain  | Drv amt   | Out lvl  | Global mix|

#include "daisy_boonta.h"

#include "Chain.h"
#include "Controls.h"
#include "LedView.h"
#include "MidiControl.h"
#include "PedalState.h"
#include "Storage.h"
#include "UsbDiag.h"

using namespace daisy;

static DaisyBoonta hw;

static PedalState state;
static Controls   controls;
static LedView    view;
static Chain      chain;
static Storage     storage(hw.seed.qspi);
static MidiControl midi;

#ifdef PROFILE_CPU
#include "util/CpuLoadMeter.h"

/** Audio callback timing, for a debugger to read. The pedal has no screen and
 *  five LEDs already spoken for, so the only way to get a number off the board
 *  is to leave it somewhere an ST-Link can see it. Uncomment the C_DEFS line in
 *  the Makefile to build this in; see the README for the gdb one-liner.
 *
 *  Volatile because nothing in the firmware ever reads these back, and the
 *  optimiser is entitled to notice that. */
static CpuLoadMeter load_meter;

static struct
{
    volatile uint32_t blocks;      /**< audio callbacks since boot */
    volatile float    avg;         /**< smoothed load, 0 to 1      */
    volatile float    min;
    volatile float    max;         /**< worst block since boot     */
    volatile float    sample_rate;
    volatile uint32_t block_size;
} profile;
#endif

#ifdef PROBE_IO
/** Peak levels either side of the DSP, for a debugger to read.
 *
 *  This answers the one question no measurement outside the box can: does the
 *  codec actually see the signal? If in_peak stays at zero while something is
 *  being played at the pedal, the fault is upstream of the DSP; if in_peak
 *  moves and out_peak does not, it is the DSP; if both move and nothing is
 *  audible, it is downstream. Uncomment the C_DEFS line in the Makefile.
 *
 *  Decaying peak hold rather than per-block peak, so a value survives long
 *  enough to be read over a link that takes a second to halt and print. */
static struct
{
    volatile float    in_l, in_r, out_l, out_r;
    volatile uint32_t blocks;
} probe;

static constexpr float kProbeDecay = 0.9995f;

static inline void ProbePeak(volatile float& held, const float* buf, size_t size)
{
    float p = held * kProbeDecay;
    for(size_t i = 0; i < size; i++)
    {
        const float a = buf[i] < 0.f ? -buf[i] : buf[i];
        if(a > p)
            p = a;
    }
    held = p;
}
#endif

// Audio-callback context: read the inputs, then render the audio. Both are
// deterministic and allocation free. The LEDs and relays are deliberately not
// touched here -- see the main loop.
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
#ifdef PROFILE_CPU
    load_meter.OnBlockStart();
#endif

    controls.Process();
    midi.Process();
    chain.Process(state, in, out, size);

#ifdef PROBE_IO
    ProbePeak(probe.in_l, in[0], size);
    ProbePeak(probe.in_r, in[1], size);
    ProbePeak(probe.out_l, out[0], size);
    ProbePeak(probe.out_r, out[1], size);
    probe.blocks++;
#endif

#ifdef PROFILE_CPU
    load_meter.OnBlockEnd();
    profile.blocks++;
    profile.avg = load_meter.GetAvgCpuLoad();
    profile.min = load_meter.GetMinCpuLoad();
    profile.max = load_meter.GetMaxCpuLoad();
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
    // asking for descriptors as soon as the port is live -- and Storage::Init
    // can block for a flash erase when it has to lay down a fresh bank.
    midi.Init(&hw, &state);

    // Settings next: Controls parks the knobs against whatever the model holds
    // on its first pass, so the restore has to have happened by then or the
    // pedal comes up on its defaults.
    storage.Init(&state, &controls);

    // Note the block size is left where Init() put it. DaisyBoonta initialises
    // its AnalogControl smoothing filters from AudioCallbackRate(), so changing
    // the block size after Init() would detune every knob on the pedal. Chain
    // copes with whatever size arrives.
    controls.Init(&hw, &state);
    view.Init(&hw);
    chain.Init(hw.AudioSampleRate());

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

        // A program change only asks; the recall itself happens here, because
        // it means writing the slot being left and that is a flash erase.
        const int preset = midi.TakePresetRequest();
        if(preset >= 0)
            storage.RequestPreset(preset);

        // Debounced, and a flash erase when it does fire -- main loop only.
        storage.Update(state);

        // Undo libDaisy's PHY clock gating, and sample the USB registers for
        // the ST-Link. Cheap enough to leave in.
        usbdiag::Service();

        System::Delay(1);
    }
}
