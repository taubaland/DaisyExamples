// Mono / stereo input conforming for Djin.
//
// The Seed has no jack-sense line, so a missing input is inferred from the
// signal: if one side is clearly playing while the other stays at the noise
// floor for a second, the pedal treats the source as mono and feeds that side
// to both channels. It returns to stereo as soon as the quiet side comes alive.
// Conservative by design: a quiet stereo source panned hard to one side will
// read as mono.
#pragma once
#include <cmath>
#include <cstddef>

namespace djin
{
class InputConform
{
  public:
    enum Mode
    {
        STEREO,
        MONO_L, /**< Left is the source, copied to right */
        MONO_R, /**< Right is the source, copied to left */
    };

    void Init(float sr)
    {
        sr_    = sr;
        decay_ = expf(-1.f / (0.2f * sr));
        slew_  = 1.f / (0.03f * sr);
        env_l_ = env_r_ = 0.f;
        cnt_            = 0;
        mode_           = STEREO;
        to_r_ = to_l_ = 0.f;
    }

    Mode GetMode() const { return mode_; }

    /** In-place: l/r hold the raw inputs on entry, the conformed inputs on exit. */
    void Process(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            env_l_ = fmaxf(fabsf(l[i]), env_l_ * decay_);
            env_r_ = fmaxf(fabsf(r[i]), env_r_ * decay_);
            Decide();

            const float tr = mode_ == MONO_L ? 1.f : 0.f;
            const float tl = mode_ == MONO_R ? 1.f : 0.f;
            to_r_ += Step(tr - to_r_);
            to_l_ += Step(tl - to_l_);

            const float rl = l[i], rr = r[i];
            l[i] = rl + to_l_ * (rr - rl);
            r[i] = rr + to_r_ * (rl - rr);
        }
    }

  private:
    static constexpr float kActive = 0.02f;  // about -34 dB
    static constexpr float kSilent = 0.004f; // about -48 dB

    float Step(float d) const
    {
        return d > slew_ ? slew_ : (d < -slew_ ? -slew_ : d);
    }

    void Decide()
    {
        const size_t hold_to_mono   = (size_t)sr_; // 1 s
        const size_t hold_to_stereo = (size_t)(0.1f * sr_);

        if(mode_ == STEREO)
        {
            Mode want = STEREO;
            if(env_l_ > kActive && env_r_ < kSilent)
                want = MONO_L;
            else if(env_r_ > kActive && env_l_ < kSilent)
                want = MONO_R;
            if(want != STEREO && want == pending_)
            {
                if(++cnt_ >= hold_to_mono)
                {
                    mode_ = want;
                    cnt_  = 0;
                }
            }
            else
            {
                pending_ = want;
                cnt_     = 0;
            }
        }
        else
        {
            const float other = mode_ == MONO_L ? env_r_ : env_l_;
            if(other > kActive)
            {
                if(++cnt_ >= hold_to_stereo)
                {
                    mode_    = STEREO;
                    pending_ = STEREO;
                    cnt_     = 0;
                }
            }
            else
                cnt_ = 0;
        }
    }

    float  sr_, decay_, slew_;
    float  env_l_, env_r_;
    float  to_r_, to_l_;
    size_t cnt_;
    Mode   mode_;
    Mode   pending_ = STEREO;
};
} // namespace djin
