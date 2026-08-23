#include "DriveEffect.h"

#include <math.h>

/** Gain range per TOG_SW_1 position: low / mid / high. */
static constexpr float kGainMin[3] = {1.f, 1.f, 4.f};
static constexpr float kGainMax[3] = {8.f, 24.f, 100.f};

/** Tone control sweep, in Hz. */
static constexpr float kToneMinHz = 250.f;
static constexpr float kToneMaxHz = 8000.f;

/** Shaper offset at either end of the BIAS knob. Beyond about half a unit the
 *  signal spends most of its time on one side of the transfer curve and the
 *  result is gated rather than asymmetric. */
static constexpr float kMaxBias = 0.5f;

/** DC blocker corner, in Hz. Low enough to be inaudible on a bass guitar, high
 *  enough to clear the offset within a note. */
static constexpr float kDcBlockHz = 10.f;

static constexpr float kTwoPi = 6.28318530718f;

/** Soft cubic clip, normalised so it reaches unity at the knee. */
static inline float SoftShape(float x)
{
    if(x >= 1.f)
        return 1.f;
    if(x <= -1.f)
        return -1.f;
    return 1.5f * (x - x * x * x / 3.f);
}

static inline float HardShape(float x)
{
    return x > 1.f ? 1.f : (x < -1.f ? -1.f : x);
}

/** Triangle wavefolder. Closed form rather than a reflect-until-inside loop:
 *  at the top of the gain range the input can be a hundred times full scale,
 *  and a loop that long has no place in an audio callback. */
static inline float FoldShape(float x)
{
    const float u = (x + 1.f) * 0.25f;
    return 4.f * fabsf(u - floorf(u + 0.5f)) - 1.f;
}

/** Make-up gain: unity at the centre of the pot, silence at the bottom,
 *  +12 dB at the top. */
static inline float LevelGain(float knob)
{
    return 4.f * knob * knob;
}

void DriveEffect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    dc_coeff_    = kTwoPi * kDcBlockHz / sample_rate_;

    for(int i = 0; i < 2; i++)
    {
        tone_z_[i] = 0.f;
        dc_z_[i]   = 0.f;
    }
}

void DriveEffect::Process(const PedalState& state,
                          float*            left,
                          float*            right,
                          size_t            size)
{
    // --- control rate: resolve the model into coefficients once per block ---
    const int range
        = static_cast<int>(state.GetToggle(PedalState::TOGGLE_DRIVE_RANGE));

    gain_ = kGainMin[range]
            * powf(kGainMax[range] / kGainMin[range],
                   state.Drive(PedalState::DRIVE_GAIN));

    bias_      = (state.Drive(PedalState::DRIVE_BIAS) - 0.5f) * 2.f * kMaxBias;
    character_ = state.Drive(PedalState::DRIVE_CHARACTER);

    const float tone_hz
        = kToneMinHz
          + state.Drive(PedalState::DRIVE_TONE) * (kToneMaxHz - kToneMinHz);
    tone_coeff_ = fminf(1.f, kTwoPi * tone_hz / sample_rate_);

    level_ = LevelGain(state.Drive(PedalState::DRIVE_LEVEL));
    mix_   = state.Drive(PedalState::DRIVE_MIX);

    // --- audio rate ----------------------------------------------------
    for(size_t i = 0; i < size; i++)
    {
        left[i]  = ProcessSample(0, left[i]);
        right[i] = ProcessSample(1, right[i]);
    }
}

float DriveEffect::ProcessSample(int channel, float in)
{
    const float driven = in * gain_ + bias_;

    // Morph across the three shapers: soft -> hard over the lower half of the
    // knob, hard -> fold over the upper half.
    float wet;
    if(character_ < 0.5f)
    {
        const float t = character_ * 2.f;
        wet           = SoftShape(driven) + t * (HardShape(driven) - SoftShape(driven));
    }
    else
    {
        const float t = (character_ - 0.5f) * 2.f;
        wet           = HardShape(driven) + t * (FoldShape(driven) - HardShape(driven));
    }

    // Remove the offset the bias put in, plus whatever asymmetric clipping
    // added on its own.
    dc_z_[channel] += dc_coeff_ * (wet - dc_z_[channel]);
    wet -= dc_z_[channel];

    // One-pole lowpass tone control.
    tone_z_[channel] += tone_coeff_ * (wet - tone_z_[channel]);
    wet = tone_z_[channel];

    wet *= level_;

    return in + mix_ * (wet - in);
}
