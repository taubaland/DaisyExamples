#include "Effect.h"

#include <math.h>

#include "daisysp.h"
#include "dev/sdram.h"

using daisysp::fclamp;
using daisysp::fonepole;
using daisysp::SoftClip;

/** The recording buffer.
 *
 *  A million frames -- about twenty-two seconds at 48 kHz -- of 16-bit
 *  stereo, four megabytes in all. That rules out internal SRAM and, more
 *  importantly, the RAM_D2_DMA region that the boot linker scripts shrink to
 *  32K, so it goes in the Seed's 64 MB of external SDRAM. DSY_SDRAM_BSS is a
 *  placement attribute on a definition and cannot follow a member into a
 *  class, hence file scope; there is exactly one granulator, so the singleton
 *  this implies costs nothing.
 *
 *  16-bit rather than float because two dozen grains each make their own
 *  scattered read every sample, and halving the bytes those reads pull out of
 *  SDRAM buys more than the discarded mantissa bits are worth. The frame
 *  count is a power of two so wrapping is a mask.
 */
namespace
{
constexpr uint32_t kBufferFrames = 1u << 20;
constexpr uint32_t kBufferMask   = kBufferFrames - 1;

int16_t DSY_SDRAM_BSS buffer[kBufferFrames][2];

constexpr float kI16ToFloat = 1.f / 32768.f;
constexpr float kFloatToI16 = 32767.f;

/** Samples kept clear between the newest grain read and the write head.
 *  Without it a grain reading at the very edge interpolates across the seam
 *  between the newest sample and the oldest one, which ticks once per pass. */
constexpr uint32_t kGuard = 256;

/** How far back a grain may be asked to start. */
constexpr uint32_t kMaxWindow = kBufferFrames - 2 * kGuard;

/** TIME sweep, in seconds. Twenty milliseconds at the bottom is a slapback
 *  that sits inside the note; twenty seconds at the top is a phrase. */
constexpr float kTimeMinSec = 0.02f;
constexpr float kTimeSpan   = 1000.f; // 0.02 * 1000 = 20 s

/** Grain length sweep, in seconds. */
constexpr float kSizeMinSec = 0.01f;
constexpr float kSizeSpan   = 100.f; // 0.01 * 100 = 1.0 s

/** Grain rate sweep, in grains per second. */
constexpr float kDensityMin  = 1.f;
constexpr float kDensitySpan = 150.f;

/** No grain may be shorter than this, and no two may start closer together.
 *  Below about a millisecond a grain is a click, not a sound. */
constexpr float    kMinIntervalSamples = 48.f;
constexpr uint32_t kMinGrainSamples    = 48;

/** Per-grain scatter per TOGGLE_SEED position: how far pitch, pan and start
 *  point are thrown about. The middle setting is a chorus; the top one is a
 *  different note every time. */
constexpr float kSeedSemis[3]  = {0.f, 3.f, 12.f};
constexpr float kSeedPan[3]    = {0.f, 0.5f, 1.f};
constexpr float kSeedOffset[3] = {0.f, 1.f, 3.f}; // in grain lengths

/** Buffer feedback per TOGGLE_FEEDBACK position. Stops short of unity: the
 *  path is soft clipped, so unity would not run off, but it would stop being
 *  a repeat and start being a drone you cannot clear. */
constexpr float kFeedback[3] = {0.f, 0.45f, 0.85f};

/** Feedback-path lowpass. Dark enough that a long tail does not turn into
 *  hiss, open enough that one repeat still sounds like the source. */
constexpr float kFbDampHz = 5000.f;

/** Recording-path gain per TOGGLE_QUALITY position. The top two settings
 *  drive into SoftClip, which is where the warmth comes from. */
constexpr float kDrive[3] = {1.f, 1.6f, 2.4f};

/** Lo-fi recording: eight bits, at a quarter of the sample rate. Applied to
 *  what goes *into* the buffer, the way Beads does it, so what comes back out
 *  is what was recorded rather than a filter on the way past. */
constexpr float    kLofiLevels    = 128.f;
constexpr uint32_t kLofiDecimate  = 4;

/** Grain envelope: a rising edge of `attack` and a falling edge of the rest,
 *  both smoothstepped.
 *
 *  This is the knob that separates this from the Clouds build. At attack near
 *  zero a grain is all decay -- a pluck. At a half it is symmetric. At near
 *  one it is all rise, which is a backwards swell. The ends are clamped off
 *  zero and one because a genuinely instantaneous edge is a click.
 *
 *  Smoothstep rather than a raised cosine: three multiplies instead of a call
 *  into libm, for a curve that is indistinguishable at this length.
 */
inline float Window(float p, float attack)
{
    const float x = (p < attack) ? (p / attack) : ((1.f - p) / (1.f - attack));
    const float c = fclamp(x, 0.f, 1.f);
    return c * c * (3.f - 2.f * c);
}

inline void ReadFrame(uint32_t i0, float frac, float& l, float& r)
{
    const uint32_t i1 = (i0 + 1) & kBufferMask;

    const float l0 = static_cast<float>(buffer[i0][0]);
    const float r0 = static_cast<float>(buffer[i0][1]);
    const float l1 = static_cast<float>(buffer[i1][0]);
    const float r1 = static_cast<float>(buffer[i1][1]);

    l = (l0 + (l1 - l0) * frac) * kI16ToFloat;
    r = (r0 + (r1 - r0) * frac) * kI16ToFloat;
}

inline void WriteFrame(uint32_t i, float l, float r)
{
    buffer[i][0] = static_cast<int16_t>(fclamp(l, -1.f, 1.f) * kFloatToI16);
    buffer[i][1] = static_cast<int16_t>(fclamp(r, -1.f, 1.f) * kFloatToI16);
}

/** Intervals the harmonic quantiser is allowed to land on, in semitones:
 *  unisons, fourths, fifths and octaves, which is the set that stays
 *  consonant with whatever is being played into it whatever key it is in. */
constexpr float kHarmonic[]
    = {-24.f, -17.f, -12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 17.f, 24.f};
constexpr int kHarmonicCount
    = static_cast<int>(sizeof(kHarmonic) / sizeof(kHarmonic[0]));

float SnapHarmonic(float semis)
{
    float best      = kHarmonic[0];
    float best_dist = fabsf(semis - best);

    for(int i = 1; i < kHarmonicCount; i++)
    {
        const float dist = fabsf(semis - kHarmonic[i]);
        if(dist < best_dist)
        {
            best      = kHarmonic[i];
            best_dist = dist;
        }
    }
    return best;
}
} // namespace

void Effect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    for(int i = 0; i < kMaxGrains; i++)
        grain_[i].active = false;

    // SDRAM comes up with whatever was in it, and grains would read that as
    // noise until the write head had been all the way round once.
    for(uint32_t i = 0; i < kBufferFrames; i++)
    {
        buffer[i][0] = 0;
        buffer[i][1] = 0;
    }

    write_       = 0;
    spawn_timer_ = 0.f;
    fb_damp_l_   = 0.f;
    fb_damp_r_   = 0.f;
    deci_hold_l_ = 0.f;
    deci_hold_r_ = 0.f;
    deci_count_  = 0;

    fb_damp_coeff_ = fclamp(TWOPI_F * kFbDampHz / sample_rate_, 0.f, 1.f);
}

uint32_t Effect::Rng()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

float Effect::RandF()
{
    return static_cast<float>(Rng() >> 8) * (1.f / 16777216.f);
}

float Effect::RandBi()
{
    return RandF() * 2.f - 1.f;
}

void Effect::UpdateParams(const PedalState& state)
{
    frozen_    = state.IsFrozen();
    quantise_  = state.GetQuantise();
    mix_       = fclamp(state.GetParam(PedalState::PARAM_MIX), 0.f, 1.f);

    // The envelope never gets a truly instantaneous edge: both ends are held
    // off the rails, because a step is a click rather than an attack.
    shape_ = fclamp(state.GetParam(PedalState::PARAM_SHAPE), 0.02f, 0.98f);

    base_semis_
        = fclamp(state.GetParam(PedalState::PARAM_PITCH), 0.f, 1.f) * 48.f
          - 24.f;

    const float size = fclamp(state.GetParam(PedalState::PARAM_SIZE), 0.f, 1.f);
    grain_len_       = static_cast<uint32_t>(
        fmaxf(kSizeMinSec * powf(kSizeSpan, size) * sample_rate_,
              static_cast<float>(kMinGrainSamples)));

    const float time = fclamp(state.GetParam(PedalState::PARAM_TIME), 0.f, 1.f);
    lag_ = kTimeMinSec * powf(kTimeSpan, time) * sample_rate_;

    // DENSITY does two things at once, as it does on Beads: how often a grain
    // starts, and how clocked that is. Fully down is a scatter of isolated
    // events; fully up is a regular, dense stream. Unlike the Clouds build,
    // the randomness falls away gradually across the whole sweep rather than
    // switching off at the centre -- so there is a usable "loose but not
    // random" region in the middle, which is where most of Beads lives.
    const float density
        = fclamp(state.GetParam(PedalState::PARAM_DENSITY), 0.f, 1.f);

    const float rate = kDensityMin * powf(kDensitySpan, density);
    interval_        = fmaxf(sample_rate_ / rate, kMinIntervalSamples);
    randomness_      = 1.f - density;

    const int seed    = static_cast<int>(state.GetToggle(PedalState::TOGGLE_SEED));
    const int fb      = static_cast<int>(state.GetToggle(PedalState::TOGGLE_FEEDBACK));
    const int quality = static_cast<int>(state.GetToggle(PedalState::TOGGLE_QUALITY));

    seed_semis_ = kSeedSemis[seed];
    seed_pan_   = kSeedPan[seed];
    scatter_    = kSeedOffset[seed] * static_cast<float>(grain_len_);
    feedback_   = kFeedback[fb];
    drive_      = kDrive[quality];
    lofi_       = (quality == 2);

    // Grains land at unrelated times, so they sum incoherently: the square
    // root of the overlap is the right normalisation, and the pool size is
    // the real ceiling on how deep that overlap can get.
    const float overlap = fminf(static_cast<float>(grain_len_) / interval_,
                                static_cast<float>(kMaxGrains));
    wet_gain_           = 1.f / sqrtf(fmaxf(1.f, overlap));
}

float Effect::GrainPitchRatio()
{
    float semis = base_semis_;

    if(seed_semis_ > 0.f)
        semis += seed_semis_ * RandBi();

    switch(quantise_)
    {
        case PedalState::QUANT_CHROMATIC: semis = roundf(semis); break;
        case PedalState::QUANT_HARMONIC: semis = SnapHarmonic(semis); break;
        case PedalState::QUANT_FREE:
        default: break;
    }

    return powf(2.f, fclamp(semis, -24.f, 24.f) * (1.f / 12.f));
}

void Effect::Spawn()
{
    // No stealing: a grain cut off part way through is a click, and a cloud
    // already running two dozen deep will not miss one.
    Grain* g = nullptr;
    for(int i = 0; i < kMaxGrains; i++)
    {
        if(!grain_[i].active)
        {
            g = &grain_[i];
            break;
        }
    }
    if(g == nullptr)
        return;

    const float inc = GrainPitchRatio();

    // A grain starting `lag` frames behind the write head reads `span` frames
    // forward over its life, so it only stays out of the write head's way
    // while lag is at least span plus the guard. Pitch is per grain here, so
    // this has to be worked out per grain too.
    const float span    = static_cast<float>(grain_len_) * inc;
    const float min_lag = span + static_cast<float>(kGuard);
    const float max_lag = fmaxf(min_lag, static_cast<float>(kMaxWindow));

    const float lag = fclamp(lag_ + scatter_ * RandBi(), min_lag, max_lag);

    g->ipos    = (write_ - static_cast<uint32_t>(lag)) & kBufferMask;
    g->frac    = 0.f;
    g->inc     = inc;
    g->env_pos = 0;
    g->env_len = grain_len_;
    g->env_rcp = 1.f / static_cast<float>(grain_len_);
    g->attack  = shape_;

    const float pan = fclamp(0.5f + 0.5f * seed_pan_ * RandBi(), 0.f, 1.f);
    const float a   = pan * HALFPI_F;
    g->pan_l        = cosf(a);
    g->pan_r        = sinf(a);

    g->active = true;
}

void Effect::Schedule()
{
    spawn_timer_ -= 1.f;
    if(spawn_timer_ > 0.f)
        return;

    Spawn();

    // Irregular spacing by the same amount that scatters everything else: a
    // sparse setting should sound like rain, not a metronome.
    const float next = interval_ * (1.f + 0.9f * randomness_ * RandBi());
    spawn_timer_ += fmaxf(next, kMinIntervalSamples);
}

void Effect::RenderGrains(float& out_l, float& out_r)
{
    float sum_l = 0.f;
    float sum_r = 0.f;

    for(int i = 0; i < kMaxGrains; i++)
    {
        Grain& g = grain_[i];
        if(!g.active)
            continue;

        float l, r;
        ReadFrame(g.ipos, g.frac, l, r);

        const float env
            = Window(static_cast<float>(g.env_pos) * g.env_rcp, g.attack);

        sum_l += l * env * g.pan_l;
        sum_r += r * env * g.pan_r;

        g.frac += g.inc;
        const uint32_t step = static_cast<uint32_t>(g.frac);
        g.frac -= static_cast<float>(step);
        g.ipos = (g.ipos + step) & kBufferMask;

        if(++g.env_pos >= g.env_len)
            g.active = false;
    }

    out_l = sum_l;
    out_r = sum_r;
}

float Effect::Process(const PedalState&   state,
                      const float* const* in,
                      float**             out,
                      size_t              size,
                      float*              in_peak)
{
    UpdateParams(state);

    const bool active = state.IsActive();

    float peak     = 0.f;
    float rec_peak = 0.f;

    for(size_t i = 0; i < size; i++)
    {
        const float dry_l = in[0][i];
        const float dry_r = in[1][i];

        float wet_l = 0.f;
        float wet_r = 0.f;

        if(active)
        {
            Schedule();
            RenderGrains(wet_l, wet_r);
            wet_l *= wet_gain_;
            wet_r *= wet_gain_;
        }

        // Record. This happens whether or not the effect is in circuit: the
        // point of twenty-two seconds of buffer is that stepping on the pedal
        // has something to play, and a buffer that only filled while engaged
        // would start from silence every time.
        if(!frozen_)
        {
            float rec_l = dry_l * drive_;
            float rec_r = dry_r * drive_;

            if(feedback_ > 0.f)
            {
                fonepole(fb_damp_l_, wet_l, fb_damp_coeff_);
                fonepole(fb_damp_r_, wet_r, fb_damp_coeff_);
                rec_l += feedback_ * fb_damp_l_;
                rec_r += feedback_ * fb_damp_r_;
            }

            rec_peak = fmaxf(rec_peak, fmaxf(fabsf(rec_l), fabsf(rec_r)));

            if(drive_ > 1.f)
            {
                // Soft clipped rather than scaled: it is what lets the drive
                // settings warm the buffer, and what keeps a high feedback
                // setting saturating into a drone instead of running off.
                rec_l = SoftClip(rec_l);
                rec_r = SoftClip(rec_r);
            }
            else
            {
                rec_l = fclamp(rec_l, -1.f, 1.f);
                rec_r = fclamp(rec_r, -1.f, 1.f);
            }

            if(lofi_)
            {
                if(deci_count_ == 0)
                {
                    deci_hold_l_
                        = roundf(rec_l * kLofiLevels) * (1.f / kLofiLevels);
                    deci_hold_r_
                        = roundf(rec_r * kLofiLevels) * (1.f / kLofiLevels);
                }
                deci_count_ = (deci_count_ + 1) % kLofiDecimate;

                rec_l = deci_hold_l_;
                rec_r = deci_hold_r_;
            }

            WriteFrame(write_, rec_l, rec_r);
            write_ = (write_ + 1) & kBufferMask;
        }

        const float o_l = active ? dry_l + mix_ * (wet_l - dry_l) : dry_l;
        const float o_r = active ? dry_r + mix_ * (wet_r - dry_r) : dry_r;

        out[0][i] = o_l;
        out[1][i] = o_r;

        peak = fmaxf(peak, fmaxf(fabsf(o_l), fabsf(o_r)));
    }

    if(in_peak != nullptr)
        *in_peak = rec_peak;

    return peak;
}
