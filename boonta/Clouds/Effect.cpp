#include "Effect.h"

#include <math.h>

#include "daisysp.h"
#include "dev/sdram.h"

using daisysp::fclamp;
using daisysp::fonepole;
using daisysp::SoftClip;

/** The recording buffer.
 *
 *  Half a million frames -- about eleven seconds at 48 kHz -- of 16-bit
 *  stereo, two megabytes in all. That rules out internal SRAM and, more
 *  importantly, the RAM_D2_DMA region that the boot linker scripts shrink to
 *  32K, so it goes in the Seed's 64 MB of external SDRAM. DSY_SDRAM_BSS is a
 *  placement attribute on a definition and cannot follow a member into a
 *  class, hence file scope; there is exactly one granulator, so the singleton
 *  this implies costs nothing.
 *
 *  16-bit rather than float for the same reason Clouds does it: two dozen
 *  grains each make their own scattered read every sample, and halving the
 *  bytes those reads pull out of SDRAM buys more than the discarded mantissa
 *  bits are worth. The frame count is a power of two so wrapping is a mask.
 */
namespace
{
constexpr uint32_t kBufferFrames = 1u << 19;
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

/** Grain length sweep. Sixteen milliseconds is short enough to be a texture
 *  rather than a note; two seconds is long enough to be a phrase. */
constexpr float kSizeMinSec = 0.016f;
constexpr float kSizeSpan   = 125.f; // 0.016 * 125 = 2.0 s

/** Grain rate sweep, in grains per second. */
constexpr float kDensityMin  = 0.5f;
constexpr float kDensitySpan = 200.f; // 0.5 * 200 = 100 /s

/** No grain may be shorter than this, and no two may start closer together.
 *  Below about a millisecond a grain is a click, not a sound. */
constexpr float    kMinIntervalSamples = 48.f;
constexpr uint32_t kMinGrainSamples    = 48;

/** Stereo scatter per TOGGLE_SPREAD position. */
constexpr float kSpread[3] = {0.f, 0.6f, 1.f};

/** Buffer feedback per TOGGLE_FEEDBACK position. Stops short of unity: the
 *  path is soft clipped, so unity would not run off, but it would stop being
 *  a repeat and start being a drone you cannot clear. */
constexpr float kFeedback[3] = {0.f, 0.45f, 0.8f};

/** Reverb blend per TOGGLE_REVERB position. */
constexpr float kReverb[3] = {0.f, 0.4f, 0.85f};

/** Reverb tail, fixed: the toggle blends more or less of the same space
 *  rather than changing the room. */
constexpr float kReverbDecay  = 0.72f;
constexpr float kReverbDampHz = 4500.f;

/** Feedback-path lowpass sweep. Swept by DENSITY in MODE_DELAY, where DENSITY
 *  has no scheduling left to do; fixed elsewhere, dark enough that a long
 *  feedback tail does not turn into hiss. */
constexpr float kFbDampMinHz     = 700.f;
constexpr float kFbDampMaxHz     = 12000.f;
constexpr float kFbDampDefaultHz = 6000.f;

/** Window fade length as a fraction of grain length, from TEXTURE. At 0.5 the
 *  grain is all curve and no flat top; at 0.02 it is very nearly a rectangle,
 *  which is the gritty end. */
constexpr float kRampMin = 0.02f;
constexpr float kRampMax = 0.5f;

/** Grain window: flat in the middle, smoothstepped at the ends.
 *
 *  Smoothstep rather than a raised cosine because it is three multiplies
 *  instead of a call into libm, and because s(x) + s(1-x) is exactly 1 --
 *  which is what lets the fixed-overlap modes crossfade two grains without a
 *  ripple in the level.
 */
inline float Window(float p, float ramp)
{
    float x;
    if(p < ramp)
        x = p / ramp;
    else if(p > 1.f - ramp)
        x = (1.f - p) / ramp;
    else
        return 1.f;

    x = fclamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
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

inline float OnePoleCoeff(float hz, float sample_rate)
{
    return fclamp(TWOPI_F * hz / sample_rate, 0.f, 1.f);
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
    next_grain_  = 0;
    lag_         = static_cast<float>(kGuard * 2);
    fb_damp_l_   = 0.f;
    fb_damp_r_   = 0.f;

    reverb_tank_.Init(sample_rate);
    reverb_tank_.SetTail(kReverbDecay, kReverbDampHz);
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

uint32_t Effect::AnchorLag() const
{
    // A grain starting `lag` frames behind the write head reads `span` frames
    // forward over its life, so it only stays out of the write head's way
    // while lag is at least span plus the guard.
    const float span    = static_cast<float>(grain_len_) * pitch_inc_;
    const float min_lag = span + static_cast<float>(kGuard);
    const float max_lag = static_cast<float>(kMaxWindow);

    if(min_lag >= max_lag)
        return static_cast<uint32_t>(max_lag);

    return static_cast<uint32_t>(min_lag + position_ * (max_lag - min_lag));
}

void Effect::UpdateParams(const PedalState& state)
{
    const PedalState::Mode mode = state.GetMode();

    // Re-anchor the playhead on a mode change rather than carry one mode's
    // position into another mode's meaning of it. The anchor depends on the
    // grain length and the pitch, so it is applied at the bottom of this
    // function, once those have been worked out for the new mode.
    const bool mode_changed = (mode != mode_);
    mode_                   = mode;

    frozen_   = state.IsFrozen();
    position_ = fclamp(state.GetParam(PedalState::PARAM_POSITION), 0.f, 1.f);
    mix_      = fclamp(state.GetParam(PedalState::PARAM_MIX), 0.f, 1.f);

    const float texture
        = fclamp(state.GetParam(PedalState::PARAM_TEXTURE), 0.f, 1.f);
    ramp_ = kRampMin + texture * (kRampMax - kRampMin);

    // Pitch: two octaves either side, continuous.
    const float pitch
        = fclamp(state.GetParam(PedalState::PARAM_PITCH), 0.f, 1.f);
    pitch_inc_ = powf(2.f, pitch * 4.f - 2.f);

    // Grain length, clamped so that the span a grain reads still fits inside
    // the window -- otherwise a two-second grain pitched up two octaves would
    // ask for eight seconds of source and lap the write head.
    const float size = fclamp(state.GetParam(PedalState::PARAM_SIZE), 0.f, 1.f);
    float       len  = kSizeMinSec * powf(kSizeSpan, size) * sample_rate_;

    const float max_span = static_cast<float>(kMaxWindow - kGuard);
    if(len * pitch_inc_ > max_span)
        len = max_span / pitch_inc_;

    grain_len_ = static_cast<uint32_t>(
        fmaxf(len, static_cast<float>(kMinGrainSamples)));

    const float density
        = fclamp(state.GetParam(PedalState::PARAM_DENSITY), 0.f, 1.f);

    spread_ = kSpread[static_cast<int>(
        state.GetToggle(PedalState::TOGGLE_SPREAD))];
    feedback_ = kFeedback[static_cast<int>(
        state.GetToggle(PedalState::TOGGLE_FEEDBACK))];
    reverb_ = kReverb[static_cast<int>(
        state.GetToggle(PedalState::TOGGLE_REVERB))];

    float fb_damp_hz = kFbDampDefaultHz;

    switch(mode_)
    {
        case PedalState::MODE_GRANULAR:
        {
            // DENSITY is two things at once, as it is on Clouds: how often a
            // grain starts, and how irregular that is. The bottom half of the
            // knob is a scatter of isolated events; the top half closes into
            // a continuous wash.
            voices_ = kMaxGrains;

            const float rate = kDensityMin * powf(kDensitySpan, density);
            interval_        = fmaxf(sample_rate_ / rate, kMinIntervalSamples);

            randomness_ = fmaxf(0.f, 1.f - 2.f * density);
            scatter_    = randomness_ * static_cast<float>(grain_len_);
            play_rate_  = 0.f;

            // Grains land at unrelated times, so they sum incoherently: the
            // square root of the overlap is the right normalisation, and the
            // pool size is the real ceiling on how deep that overlap can get.
            const float overlap
                = fminf(static_cast<float>(grain_len_) / interval_,
                        static_cast<float>(kMaxGrains));
            wet_gain_ = 1.f / sqrtf(fmaxf(1.f, overlap));
            break;
        }

        case PedalState::MODE_STRETCH:
        {
            // Two grains crossfading, with the hop set so that the two
            // windows sum to exactly one -- see Window(). DENSITY becomes the
            // playback rate: stopped, through real time at the centre, to
            // double speed at the top.
            voices_     = 2;
            interval_   = fmaxf(static_cast<float>(grain_len_) * (1.f - ramp_),
                              kMinIntervalSamples);
            randomness_ = 0.f;
            scatter_    = 0.f;
            play_rate_  = density * 2.f;
            wet_gain_   = 1.f;
            break;
        }

        case PedalState::MODE_DELAY:
        default:
        {
            // The same two-grain crossfade with the playhead locked to real
            // time: at unity pitch that is a plain delay, off unity it is a
            // transposing one. DENSITY has no scheduling left to do here, so
            // it damps the feedback path instead.
            voices_     = 2;
            interval_   = fmaxf(static_cast<float>(grain_len_) * (1.f - ramp_),
                              kMinIntervalSamples);
            randomness_ = 0.f;
            scatter_    = 0.f;
            play_rate_  = 1.f;
            wet_gain_   = 1.f;

            fb_damp_hz = kFbDampMinHz + density * (kFbDampMaxHz - kFbDampMinHz);

            // POSITION is the delay time, so follow it while the buffer is
            // running. Once frozen the playhead is let go and loops whatever
            // was last set, which is what turns the delay into a looper.
            if(!frozen_)
                lag_ = static_cast<float>(AnchorLag());
            break;
        }
    }

    if(mode_changed)
        lag_ = static_cast<float>(AnchorLag());

    fb_damp_coeff_ = OnePoleCoeff(fb_damp_hz, sample_rate_);

    if(next_grain_ >= voices_)
        next_grain_ = 0;
}

void Effect::Spawn(uint32_t start_lag)
{
    Grain* g    = nullptr;
    int    slot = next_grain_;

    if(mode_ == PedalState::MODE_GRANULAR)
    {
        // No stealing: a grain cut off part way through is a click, and a
        // cloud already running two dozen deep will not miss one.
        for(int i = 0; i < kMaxGrains; i++)
        {
            if(!grain_[i].active)
            {
                g    = &grain_[i];
                slot = i;
                break;
            }
        }
        if(g == nullptr)
            return;
    }
    else
    {
        // Round robin over as many slots as the mode overlaps. The hop is the
        // grain length minus the crossfade, so the slot coming up for reuse
        // is the one that has just finished.
        g           = &grain_[next_grain_];
        slot        = next_grain_;
        next_grain_ = (next_grain_ + 1) % voices_;
    }

    g->ipos    = (write_ - start_lag) & kBufferMask;
    g->frac    = 0.f;
    g->inc     = pitch_inc_;
    g->env_pos = 0;
    g->env_len = grain_len_;
    g->env_rcp = 1.f / static_cast<float>(grain_len_);
    g->ramp    = ramp_;

    // Panning. Random in the cloud, where scattering the grains across the
    // image is the whole point of the spread control; alternating in the
    // fixed modes, where random placement reads as the image wobbling rather
    // than as width.
    const float pan = (mode_ == PedalState::MODE_GRANULAR)
                          ? 0.5f + 0.5f * spread_ * RandBi()
                          : 0.5f + ((slot & 1) ? 0.5f : -0.5f) * spread_;

    const float a = fclamp(pan, 0.f, 1.f) * HALFPI_F;
    g->pan_l      = cosf(a);
    g->pan_r      = sinf(a);

    g->active = true;
}

void Effect::Schedule()
{
    spawn_timer_ -= 1.f;
    if(spawn_timer_ > 0.f)
        return;

    float    next = interval_;
    uint32_t start_lag;

    if(mode_ == PedalState::MODE_GRANULAR)
    {
        const float span    = static_cast<float>(grain_len_) * pitch_inc_;
        const float min_lag = span + static_cast<float>(kGuard);
        const float max_lag = fmaxf(min_lag, static_cast<float>(kMaxWindow));

        const float lag = static_cast<float>(AnchorLag()) + scatter_ * RandBi();
        start_lag       = static_cast<uint32_t>(fclamp(lag, min_lag, max_lag));

        // Irregular spacing, by the same amount that scatters the start
        // points: a sparse setting should sound like weather, not a clock.
        next = interval_ * (1.f + 0.9f * randomness_ * RandBi());
    }
    else
    {
        start_lag = static_cast<uint32_t>(lag_);
    }

    Spawn(start_lag);

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
            = Window(static_cast<float>(g.env_pos) * g.env_rcp, g.ramp);

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
                      size_t              size)
{
    UpdateParams(state);

    const bool active = state.IsActive();

    // The playhead's distance behind the write head may not leave the window.
    const float span    = static_cast<float>(grain_len_) * pitch_inc_;
    const float min_lag = span + static_cast<float>(kGuard);
    const float max_lag = fmaxf(min_lag, static_cast<float>(kMaxWindow));
    const float anchor  = fmaxf(static_cast<float>(AnchorLag()), min_lag);

    // How fast the playhead falls behind: the write head moves a sample per
    // sample unless it is frozen, and the playhead moves play_rate_. Freezing
    // is therefore the only thing that lets a playhead running at real-time
    // rate actually travel through the buffer.
    const float lag_delta = (frozen_ ? 0.f : 1.f) - play_rate_;
    const bool  moving    = (mode_ != PedalState::MODE_GRANULAR);

    float peak = 0.f;

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

            if(reverb_ > 0.f)
            {
                float rev_l, rev_r;
                reverb_tank_.Process(wet_l, wet_r, rev_l, rev_r);

                // Blend rather than add, so turning the tail up does not also
                // turn the whole wet path up.
                wet_l = wet_l * (1.f - 0.5f * reverb_) + rev_l * reverb_;
                wet_r = wet_r * (1.f - 0.5f * reverb_) + rev_r * reverb_;
            }
        }

        // Record. This happens whether or not the effect is in circuit: the
        // point of eleven seconds of buffer is that stepping on the pedal has
        // something to play, and a buffer that only filled while engaged
        // would start from silence every time.
        if(!frozen_)
        {
            float rec_l = dry_l;
            float rec_r = dry_r;

            if(feedback_ > 0.f)
            {
                fonepole(fb_damp_l_, wet_l, fb_damp_coeff_);
                fonepole(fb_damp_r_, wet_r, fb_damp_coeff_);

                // Soft clipped rather than scaled: it is what lets the high
                // feedback setting saturate into a drone instead of either
                // dying away or running off.
                rec_l = SoftClip(rec_l + feedback_ * fb_damp_l_);
                rec_r = SoftClip(rec_r + feedback_ * fb_damp_r_);
            }

            WriteFrame(write_, rec_l, rec_r);
            write_ = (write_ + 1) & kBufferMask;
        }

        if(moving)
        {
            lag_ += lag_delta;

            // Running off the newest end means the playhead has caught up
            // with the recording, so send it back to where POSITION says the
            // loop starts. Running off the oldest end means it has run out of
            // history, so put it back on the newest material there is.
            if(lag_ < min_lag)
                lag_ = anchor;
            else if(lag_ > max_lag)
                lag_ = min_lag;
        }

        const float o_l = active ? dry_l + mix_ * (wet_l - dry_l) : dry_l;
        const float o_r = active ? dry_r + mix_ * (wet_r - dry_r) : dry_r;

        out[0][i] = o_l;
        out[1][i] = o_r;

        peak = fmaxf(peak, fmaxf(fabsf(o_l), fabsf(o_r)));
    }

    return peak;
}
