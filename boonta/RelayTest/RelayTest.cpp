// # Boonta RelayTest
//
// Bench test for the two relays and the output stage. It steps through all
// four relay combinations forever, three seconds each, while the Daisy plays
// a different tone on each channel and the codec input is measured.
//
//   step  K1 (D22, output)  K2 (D23, input)
//   ----  ----------------  ----------------
//    0    off               off               bypass
//    1    on                on                in circuit
//    2    on                off
//    3    off               on
//
// ## Signals
// The Daisy never passes its input through. It plays only its own tones, so
// whatever shows up at the output jack can be traced to a source by frequency:
//
//   Daisy DAC     left 2000 Hz, right 3000 Hz, both at -20 dBFS
//   External      left 1000 Hz, right 1500 Hz, fed into the input jack
//
// What each step should show at the output jack on a correctly wired board:
//
//   step 0   1000 / 1500 Hz only    dry path through K2; DAC grounded by K1
//   step 1   2000 / 3000 Hz only    DAC through K1; K2 breaks the dry path
//   step 2   2000 / 3000 Hz, plus   K1 passes the DAC, K2 still offers the
//            1000 / 1500 Hz         dry path to the same jack
//   step 3   silence                nothing reaches the jack; K1 grounds it
//
// ## Serial
// One line per step over USB serial (115200, any terminal), printed at the end
// of the step so the host can line the recording up with the relay state.
// inL and inR are the RMS of the codec input as a fraction of full scale,
// times 1000, as integers (so 100 means 0.1, about -20 dBFS):
//
//   STEP <n> K1=<0|1> K2=<0|1> t=<ms> inL=<milli> inR=<milli>
//
// The footswitch LEDs show the state: LED 1 is green while K1 is on, LED 2 is
// blue while K2 is on. Hold SW_FS_1 to freeze on the current step.

#include "daisy_boonta.h"

#include <math.h>

using namespace daisy;

static DaisyBoonta hw;

static constexpr uint32_t kStepMs   = 3000;
static constexpr float    kLevel    = 0.1f; // -20 dBFS
static constexpr float    kFreqL    = 2000.f;
static constexpr float    kFreqR    = 3000.f;
static constexpr float    kTwoPi    = 6.28318530718f;
static constexpr int      kSteps    = 4;
static constexpr bool     kK1[kSteps] = {false, true, true, false};
static constexpr bool     kK2[kSteps] = {false, true, false, true};

static float phase_l = 0.f;
static float phase_r = 0.f;
static float inc_l   = 0.f;
static float inc_r   = 0.f;

// Input meter. Written by the audio callback, read and cleared by the main
// loop at each step change; a torn read costs one inaccurate line, not a fault.
static volatile float sum_l   = 0.f;
static volatile float sum_r   = 0.f;
static volatile uint32_t nsamp = 0;

static void Audio(AudioHandle::InputBuffer  in,
                  AudioHandle::OutputBuffer out,
                  size_t                    size)
{
    float sl = 0.f, sr = 0.f;
    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = kLevel * sinf(phase_l);
        out[1][i] = kLevel * sinf(phase_r);

        phase_l += inc_l;
        if(phase_l >= kTwoPi)
            phase_l -= kTwoPi;
        phase_r += inc_r;
        if(phase_r >= kTwoPi)
            phase_r -= kTwoPi;

        sl += in[0][i] * in[0][i];
        sr += in[1][i] * in[1][i];
    }
    sum_l = sum_l + sl;
    sum_r = sum_r + sr;
    nsamp = nsamp + size;
}

static void SetStep(int step)
{
    hw.relay_output.Write(kK1[step]);
    hw.relay_input.Write(kK2[step]);

    hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1,
                        0.f,
                        kK1[step] ? 1.f : 0.f,
                        0.f);
    hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2,
                        0.f,
                        0.f,
                        kK2[step] ? 1.f : 0.f);
    hw.UpdateLeds();
}

int main(void)
{
    hw.Init();
    hw.seed.StartLog(false); // do not wait for a terminal

    const float sr = hw.AudioSampleRate();
    inc_l          = kTwoPi * kFreqL / sr;
    inc_r          = kTwoPi * kFreqR / sr;

    // Boot with both relays off, the same as the pedal's own bypass.
    int step = 0;
    SetStep(step);
    hw.StartAudio(Audio);

    uint32_t step_start = System::GetNow();

    while(1)
    {
        hw.ProcessAllControls();

        const uint32_t now = System::GetNow();
        if(now - step_start < kStepMs)
            continue;

        // Close out the step that just ended.
        const uint32_t n = nsamp;
        const float    l = n ? sqrtf(sum_l / n) : 0.f;
        const float    r = n ? sqrtf(sum_r / n) : 0.f;
        sum_l            = 0.f;
        sum_r            = 0.f;
        nsamp            = 0;

        hw.seed.PrintLine("STEP %d K1=%d K2=%d t=%lu inL=%d inR=%d",
                          step,
                          kK1[step] ? 1 : 0,
                          kK2[step] ? 1 : 0,
                          static_cast<unsigned long>(now),
                          static_cast<int>(l * 1000.f),
                          static_cast<int>(r * 1000.f));

        if(!hw.switches[DaisyBoonta::SW_FS_1].Pressed())
            step = (step + 1) % kSteps;
        SetStep(step);
        step_start = now;
    }
}
