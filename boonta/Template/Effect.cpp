#include "Effect.h"

#include <math.h>

#include "daisysp.h"

using namespace daisysp;

/** Gain range per RANGE toggle position. */
static constexpr float kDriveMin[3] = {1.f, 1.f, 4.f};  // low, mid, high
static constexpr float kDriveMax[3] = {8.f, 24.f, 80.f};

/** Tone control sweep, in Hz. */
static constexpr float kToneMinHz = 250.f;
static constexpr float kToneMaxHz = 8000.f;

/** Extra gain while the alt footswitch is held. */
static constexpr float kBoost = 3.f;

void Effect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    for(int i = 0; i < 2; i++)
    {
        tone_z_[i] = 0.f;
        dc_z_[i]   = 0.f;
    }
}

void Effect::Process(const PedalState&   state,
                     const float* const* in,
                     float**             out,
                     size_t              size)
{
    // Hard bypass: the relays route around us, so just stay quiet and let the
    // filter state settle rather than burning cycles.
    if(state.IsBypassed())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
        }
        return;
    }

    // --- control rate: resolve the model into coefficients once per block ---
    const int range = static_cast<int>(state.GetToggle(PedalState::TOGGLE_RANGE));

    drive_ = kDriveMin[range]
             + state.GetParam(PedalState::PARAM_DRIVE)
                   * (kDriveMax[range] - kDriveMin[range]);
    if(state.AltHeld())
        drive_ *= kBoost;

    const float tone_hz
        = kToneMinHz
          + state.GetParam(PedalState::PARAM_TONE) * (kToneMaxHz - kToneMinHz);
    tone_coeff_ = fminf(1.f, TWOPI_F * tone_hz / sample_rate_);

    level_ = state.GetParam(PedalState::PARAM_LEVEL);
    mix_   = state.GetParam(PedalState::PARAM_MIX);

    // --- audio rate ----------------------------------------------------
    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = ProcessChannel(0, in[0][i]);
        out[1][i] = ProcessChannel(1, in[1][i]);
    }
}

float Effect::ProcessChannel(int channel, float in)
{
    float wet = SoftClip(in * drive_);

    // One-pole lowpass tone control.
    fonepole(tone_z_[channel], wet, tone_coeff_);
    wet = tone_z_[channel];

    // Remove the DC that asymmetric clipping leaves behind.
    fonepole(dc_z_[channel], wet, 0.001f);
    wet -= dc_z_[channel];

    wet *= level_;

    return in + mix_ * (wet - in);
}
