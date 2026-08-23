#include "EqEffect.h"

/** Band sweeps, in Hz. They overlap deliberately: the point of a sweepable
 *  three band is being able to put two bands on the same problem. */
static constexpr float kLowMinHz = 40.f, kLowMaxHz = 500.f;
static constexpr float kMidMinHz = 200.f, kMidMaxHz = 4000.f;
static constexpr float kHighMinHz = 1500.f, kHighMaxHz = 12000.f;

/** Cut and boost either side of the centre detent. */
static constexpr float kMaxGainDb = 15.f;

/** Band width per TOG_SW_3 position: low / mid / high on the switch reads as
 *  wide / medium / tight. Shelves take a slope, the peak takes a Q; both are
 *  listed here so the three bands stay in step. */
static constexpr float kShelfSlope[3] = {0.4f, 0.7f, 1.0f};
static constexpr float kMidQ[3]       = {0.5f, 1.1f, 3.0f};

/** Below this much knob movement the coefficients are left alone. A tenth of a
 *  percent of travel is far under the ADC noise floor after smoothing, so this
 *  only ever skips redesigns that would have produced the same filter. */
static constexpr float kRedesignEpsilon = 0.001f;

/** Exponential knob-to-frequency map, so an octave is the same distance
 *  everywhere on the pot. */
static inline float SweepHz(float knob, float min_hz, float max_hz)
{
    return min_hz * powf(max_hz / min_hz, knob);
}

/** Centre detent is flat, either end is full cut or full boost. */
static inline float GainDb(float knob)
{
    return (knob - 0.5f) * 2.f * kMaxGainDb;
}

void EqEffect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    low_.SetIdentity();
    mid_.SetIdentity();
    high_.SetIdentity();

    for(int i = 0; i < 2; i++)
    {
        low_z_[i].Reset();
        mid_z_[i].Reset();
        high_z_[i].Reset();
    }

    coeffs_valid_ = false;
}

void EqEffect::UpdateCoeffs(const PedalState& state)
{
    const int q_index = static_cast<int>(state.GetToggle(PedalState::TOGGLE_EQ_Q));

    bool changed = !coeffs_valid_ || q_index != coeffs_from_q_;

    for(int i = 0; i < PedalState::kKnobCount && !changed; i++)
        changed = fabsf(state.GetKnob(PedalState::PAGE_EQ, i) - coeffs_from_[i])
                  > kRedesignEpsilon;

    if(!changed)
        return;

    for(int i = 0; i < PedalState::kKnobCount; i++)
        coeffs_from_[i] = state.GetKnob(PedalState::PAGE_EQ, i);
    coeffs_from_q_ = q_index;
    coeffs_valid_  = true;

    const float slope = kShelfSlope[q_index];

    low_.SetLowShelf(sample_rate_,
                     SweepHz(state.Eq(PedalState::EQ_LOW_FREQ), kLowMinHz, kLowMaxHz),
                     GainDb(state.Eq(PedalState::EQ_LOW_GAIN)),
                     slope);

    mid_.SetPeaking(sample_rate_,
                    SweepHz(state.Eq(PedalState::EQ_MID_FREQ), kMidMinHz, kMidMaxHz),
                    GainDb(state.Eq(PedalState::EQ_MID_GAIN)),
                    kMidQ[q_index]);

    high_.SetHighShelf(sample_rate_,
                       SweepHz(state.Eq(PedalState::EQ_HIGH_FREQ), kHighMinHz, kHighMaxHz),
                       GainDb(state.Eq(PedalState::EQ_HIGH_GAIN)),
                       slope);
}

void EqEffect::Process(const PedalState& state,
                       float*            left,
                       float*            right,
                       size_t            size)
{
    UpdateCoeffs(state);

    float* channel[2] = {left, right};

    for(int c = 0; c < 2; c++)
    {
        for(size_t i = 0; i < size; i++)
        {
            float x = channel[c][i];
            x       = low_z_[c].Process(low_, x);
            x       = mid_z_[c].Process(mid_, x);
            x       = high_z_[c].Process(high_, x);
            channel[c][i] = x;
        }
    }
}
