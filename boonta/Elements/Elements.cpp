// # Boonta Elements
//
// Mutable Instruments' Elements -- a modal synthesis voice: bow, blow and
// strike exciters into a resonator -- on the Boonta pedal, with the input
// jack as a fourth exciter. Built on the Boonta template's split along MVC
// lines:
//
//   PedalState  (model)      what the pedal is set to. No hardware.
//   Controls    (controller) knobs/switches/expression -> model.
//   MidiControl (controller) USB and DIN MIDI -> the same model.
//   Persist     (controller) the model <-> flash, across a power cycle.
//   LedView     (view)       model -> LEDs and bypass relays.
//   Effect      (dsp)        model -> samples, by way of elements::Part.
//   Samples                  Elements' sample tables, flash -> SDRAM.
//   elements/                upstream's DSP, unmodified. MIT, Emilie Gillet.
//   Elements.cpp             wiring, and nothing else.
//
// ## Controls
// | Control  | Function                                                      |
// |----------|---------------------------------------------------------------|
// | KNOB_1-6 | Page 1: Pitch, Geometry, Brightness, Damping, Position, Space  |
// |          | Page 2: Contour, Bow, Blow, Strike, Input, Strength            |
// |          | Page 3: Bow timbre, Flow, Blow timbre, Mallet, Strike timbre,  |
// |          |         Fine                                                   |
// | TOG_SW_1 | Resonator: modal / string / chord of strings                   |
// | TOG_SW_2 | Trigger: manual / the input's attacks / drone                  |
// | TOG_SW_3 | Input to: strike / both / blow                                 |
// | SW_SEL_1 | Previous knob page                                             |
// | SW_SEL_2 | Next knob page                                                 |
// | SW_FS_1  | Gate: hold to play, hold past 700 ms to latch                  |
// | SW_FS_2  | Bypass (latching, drives the relays)                           |
// | EXP      | Geometry, once moved                                           |
// | MIDI     | Notes play it; CC 102-119 and 20-31, see MidiMap.h             |

#include <stdlib.h>

#include "daisy_boonta.h"

#include "Controls.h"
#include "Effect.h"
#include "LedView.h"
#include "MidiControl.h"
#include "PedalState.h"
#include "Persist.h"
#include "Samples.h"
#include "UsbDiag.h"

#include "elements/dsp/part.h"

using namespace daisy;

static DaisyBoonta hw;

static PedalState  state;
static Controls    controls;
static MidiControl midi;
static Persist     persist(hw.seed.qspi);
static LedView     view;
static Effect      effect;

/** Elements' native block is sixteen samples at 32 kHz. The callback is run
 *  at twice that so it comes round a thousand times a second, which is the
 *  rate DaisyBoonta tunes its knob smoothing and switch debouncing for;
 *  Effect cuts each block in two. */
static constexpr size_t kBlockSize = 32;

#ifdef PROFILE_CPU
/** Callback load, kept in a global so an ST-Link can read it:
 *
 *      (gdb) print profile
 *
 *  Each field is a fraction of the block period, so max >= 1.0 means the
 *  callback overran at least once and audio dropped out. PROFILE_CPU is set
 *  in the Makefile.
 *
 *  Volatile because nothing in the firmware ever reads these back. */
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

/** Clear the worst-block figure without rebooting: write 1 to this. */
volatile uint32_t profile_reset = 0;

/** Measure the DSP without putting the pedal in circuit.
 *
 *  Bypassed, Effect renders nothing, so the load reads as a couple of
 *  percent whatever the settings; and taking it out of bypass over the
 *  debugger closes the relays and sends whatever it is playing to the amp.
 *  With bit 0 set here the callback renders anyway, from a copy of the model
 *  with bypass cleared, and throws the audio away: the relays never move.
 *  Bit 1 also holds the gate up in that copy, which is the expensive case.
 *  Bit 2 feeds it a 196 Hz sawtooth instead of the jack. */
volatile uint32_t profile_render = 0;
#endif

// Audio-callback context: read the inputs, then render the audio. All of it
// is deterministic and allocation free. The LEDs and relays are deliberately
// not touched here -- see the main loop.
//
// Controls runs before MidiControl so that the remote has the last word
// within a block; which of them keeps a parameter over time is settled by
// the pickup rule in Controls, not by this ordering.
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
#ifdef PROFILE_CPU
    load_meter.OnBlockStart();
#endif

    controls.Process();
    midi.Process();

#ifdef PROFILE_CPU
    if(profile_render != 0 && state.IsBypassed())
    {
        PedalState shadow = state;
        shadow.SetBypass(false);
        shadow.SetGateRemote((profile_render & 2) != 0);

        // Bit 2 replaces the jack with a synthetic plucked note, so the
        // pitch tracker has something to analyse: on a quiet jack it skips
        // its analysis, and the load it reports is not the one a guitar
        // would cost.
        float        synth[kBlockSize];
        const float* synth_in[2] = {synth, synth};
        if(profile_render & 4)
        {
            static float phase = 0.f;
            for(size_t i = 0; i < size; i++)
            {
                phase += 196.f / 32000.f;
                if(phase >= 1.f)
                    phase -= 1.f;
                synth[i] = 0.3f * (2.f * phase - 1.f);
            }
            effect.Process(shadow, synth_in, out, size);
        }
        else
            effect.Process(shadow, in, out, size);
        for(size_t i = 0; i < size; i++)
            out[0][i] = out[1][i] = 0.f;
        state.SetOutputLevel(0.f);
    }
    else
#endif
        state.SetOutputLevel(effect.Process(state, in, out, size));

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

    // Elements is a 32 kHz instrument and its tables are computed for it.
    // The codec is happy there, so it is the codec that moves.
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_32KHZ);
    hw.SetAudioBlockSize(kBlockSize);

    // Trim HSI48 before USB comes up. See UsbDiag.h.
    usbdiag::EnableCrs();

    // MIDI first. USB device enumeration is time-critical -- the host starts
    // asking for descriptors as soon as the port is live -- and the sample
    // tables take a moment to check and copy.
    midi.Init(&hw, &state);

    samples::Load(hw.seed.qspi);

    // Saved state goes into the model before the panel is read, so that
    // Controls can hand it the knobs only once they have been turned to
    // within reach of it.
    const bool restored = persist.Init(&state);

    controls.Init(&hw, &state, restored);
    view.Init(&hw);

    // The Part and its reverb line are 180 KB between them, more than the
    // 128 KB of DTCMRAM a BOOT_SRAM build keeps its variables in. The heap
    // is in the D2 SRAM, which has the room and is as fast. calloc and not
    // malloc: upstream keeps the Part in .bss and relies on it being zero.
    // Allocated once, never freed.
    elements::Part* part
        = static_cast<elements::Part*>(calloc(1, sizeof(elements::Part)));
    uint16_t* reverb_buffer
        = static_cast<uint16_t*>(calloc(32768, sizeof(uint16_t)));

    // Nothing useful can happen without them, and limping on would mean
    // writing through a null pointer in the audio callback. Stop where a
    // debugger will find it, with the relays still in bypass.
    while(part == nullptr || reverb_buffer == nullptr) {}

    // The chip's unique ID stands in for the module's serial number.
    effect.Init(part, reverb_buffer, reinterpret_cast<uint32_t*>(UID_BASE), 3);

#ifdef PROFILE_CPU
    profile.sample_rate = hw.seed.AudioSampleRate();
    profile.block_size  = hw.AudioBlockSize();
    load_meter.Init(profile.sample_rate, profile.block_size);
#endif

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    // Main-loop context: anything slow, blocking or mechanical.
    while(1)
    {
        state.SetSamplesLoaded(samples::loaded != 0);
        view.Update(state);

        // Flash writes block, so they live here and not in the callback.
        persist.Update(System::GetNow());

        // Undo the PHY clock gating libDaisy's suspend callback leaves behind.
        usbdiag::Service();

        System::Delay(1);
    }
}
