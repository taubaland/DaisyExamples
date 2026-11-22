// Shared output chain for every Djin script: tape emulation, a light
// compressor, then a peak limiter at -0.2 dB. All stereo, one sample at a time.
#pragma once
#include <cmath>

namespace djin
{
// Gentle 15 ips tape colouring: soft saturation, a little low-end head bump and
// a high-frequency roll-off. Unity gain for small signals.
class TapeEmu
{
  public:
    void Init(float sr)
    {
        sr_       = sr;
        lp_coef_  = OnePole(14000.f);
        bump_     = OnePole(90.f);
        lp_l_ = lp_r_ = bump_l_ = bump_r_ = 0.f;
    }

    void Process(float& l, float& r)
    {
        l = Channel(l, lp_l_, bump_l_);
        r = Channel(r, lp_r_, bump_r_);
    }

  private:
    static constexpr float kDrive    = 1.3f;
    static constexpr float kBumpGain = 0.18f;

    float OnePole(float fc) const
    {
        return 1.f - expf(-6.2831853f * fc / sr_);
    }

    float Channel(float x, float& lp, float& bump) const
    {
        // Head bump: add a little of the low band back in.
        bump += bump_ * (x - bump);
        float y = x + kBumpGain * bump;
        // Soft saturation, normalised so small signals pass at unity.
        y = tanhf(kDrive * y) / kDrive;
        // HF roll-off.
        lp += lp_coef_ * (y - lp);
        return lp;
    }

    float sr_, lp_coef_, bump_;
    float lp_l_, lp_r_, bump_l_, bump_r_;
};

// Linked-stereo feed-forward compressor that only touches the top peaks.
class Compressor
{
  public:
    void Init(float sr)
    {
        atk_ = expf(-1.f / (0.003f * sr));
        rel_ = expf(-1.f / (0.120f * sr));
        env_ = 0.f;
    }

    void Process(float& l, float& r)
    {
        const float lvl = fmaxf(fabsf(l), fabsf(r));
        env_ = lvl > env_ ? atk_ * env_ + (1.f - atk_) * lvl
                          : rel_ * env_ + (1.f - rel_) * lvl;
        const float db   = 20.f * log10f(env_ + 1e-9f);
        const float over = db - kThresholdDb;
        float       gr_db = 0.f;
        if(over > kKneeDb * 0.5f)
            gr_db = -over * (1.f - 1.f / kRatio);
        else if(over > -kKneeDb * 0.5f)
        {
            const float x = over + kKneeDb * 0.5f; // soft knee
            gr_db         = -(1.f - 1.f / kRatio) * x * x / (2.f * kKneeDb);
        }
        const float g = powf(10.f, gr_db / 20.f);
        l *= g;
        r *= g;
    }

  private:
    static constexpr float kThresholdDb = -8.f;
    static constexpr float kRatio       = 2.5f;
    static constexpr float kKneeDb      = 6.f;
    float                  atk_, rel_, env_;
};

// Brick-wall peak limiter: instant attack, 50 ms release, ceiling -0.2 dB.
class Limiter
{
  public:
    void Init(float sr)
    {
        rel_  = 1.f - expf(-1.f / (0.050f * sr));
        gain_ = 1.f;
    }

    void Process(float& l, float& r)
    {
        const float peak   = fmaxf(fabsf(l), fabsf(r));
        const float target = peak > kCeiling ? kCeiling / peak : 1.f;
        if(target < gain_)
            gain_ = target;
        else
            gain_ += (target - gain_) * rel_;
        l = Clamp(l * gain_);
        r = Clamp(r * gain_);
    }

  private:
    // 10^(-0.2 / 20)
    static constexpr float kCeiling = 0.97724f;
    static float           Clamp(float x)
    {
        return x > kCeiling ? kCeiling : (x < -kCeiling ? -kCeiling : x);
    }
    float rel_, gain_;
};

} // namespace djin
