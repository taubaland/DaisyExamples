// Script One: a stereo cut-and-paste looper after the Montreal Assembly Count
// To Five (Mode 2/3).
//
// Two 5 second circular buffers (left and right, in SDRAM) record continuously.
// Four voices (two per buffer) repeatedly grab a slice from their buffer and
// play it back. Voices on the same buffer are staggered by half a slice so
// slices overlap.
//
// Length encoder : slice size, tiny stutters (CCW) to long phrases (CW). Each
//                  slice also drifts a little in size by itself.
// Main encoder   : -18..+18 detents. One position drives three things:
//                    direction   0 = 50/50 forward/reverse, +-2..5 = all forward
//                                or all reverse
//                    slow slices -6..-12 / 6..12 mix in half-speed slices,
//                                +-13..18 are all half-speed and ease slower
//                    randomness  |pos| / 18 = chance a slice jumps to a new
//                                random place in the buffer
//                  Slower playback keeps its pitch (granular time-stretch
//                  made of 50 ms overlapped micro-grains read at native speed).
//
// The two buffers interact slightly: each records a little of the other
// channel, and a voice sometimes grabs near where its partner voice on the other
// buffer is reading. Slices are panned to a random fixed spot (left buffer
// leans left, right buffer leans right, with overlap in the middle).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include "daisy_core.h"
#include "djin_script.h"

namespace djin
{
class ScriptOne : public Script
{
  public:
    static constexpr size_t kBufLen = 240000; // 5 s at 48 kHz

    void Init(float sr) override
    {
        sr_ = sr;
        for(int n = 0; n < kW; n++)
            hann_[n] = 0.5f - 0.5f * cosf(6.2831853f * n / kW);
        Activate();
    }

    void Activate() override
    {
        std::memset(buf_[0], 0, sizeof(float) * kBufLen);
        std::memset(buf_[1], 0, sizeof(float) * kBufLen);
        wi_     = 0;
        filled_ = 0;
        main_   = 0;
        len_    = kDefaultLen;
        flash_  = false;
        for(int i = 0; i < kVoices; i++)
        {
            Voice& v = voice_[i];
            v        = Voice();
            v.buf    = buf_[i / 2];
            v.id     = i;
        }
        // Stagger the second voice of each buffer by half a slice.
        const int half = (int)(LenSamples() * 0.5f);
        voice_[1].delay = voice_[3].delay = half;
    }

    void MainEncoder(int inc) override
    {
        main_ += inc;
        if(main_ > kMainMax)
            main_ = kMainMax;
        if(main_ < -kMainMax)
            main_ = -kMainMax;
    }

    void LengthEncoder(int inc) override
    {
        const int old = len_;
        len_ += inc;
        if(len_ > kLenMax)
            len_ = kLenMax;
        if(len_ < 0)
            len_ = 0;
        if(len_ == old)
            return;
        // Respond now, not at the end of the current slice (which can be
        // seconds away): fade the playing slices out and restart them at the
        // new length. The second voice of each buffer re-staggers by half a
        // slice so the two keep overlapping.
        for(int i = 0; i < kVoices; i++)
        {
            Voice& v = voice_[i];
            if(v.active)
            {
                const int end = v.m + (int)kFadeSamples;
                if(end < v.dur)
                    v.dur = end;
            }
            v.stagger = (i & 1) != 0;
        }
    }

    bool TakeFlash() override
    {
        const bool f = flash_;
        flash_       = false;
        return f;
    }

    int MainPos() const { return main_; }
    int LenStep() const { return len_; }

    void Process(const float* in_l,
                 const float* in_r,
                 float*       out_l,
                 float*       out_r,
                 size_t       n) override
    {
        for(size_t i = 0; i < n; i++)
        {
            buf_[0][wi_] = in_l[i] + kCrossRecord * in_r[i];
            buf_[1][wi_] = in_r[i] + kCrossRecord * in_l[i];

            float l = 0.f, r = 0.f;
            for(int v = 0; v < kVoices; v++)
            {
                Voice& vo = voice_[v];
                const float s = Tick(vo);
                l += s * vo.pan_l;
                r += s * vo.pan_r;
            }
            out_l[i] = l * kVoiceGain;
            out_r[i] = r * kVoiceGain;

            if(++wi_ >= (int)kBufLen)
                wi_ = 0;
            if(filled_ < (int)kBufLen)
                filled_++;
        }
    }

  private:
    static constexpr int   kVoices      = 4;
    static constexpr int   kH           = 1200; // micro-grain hop, 25 ms
    static constexpr int   kW           = 2 * kH;
    static constexpr int   kMainMax     = 18;
    static constexpr int   kLenMax      = 24;
    static constexpr int   kDefaultLen  = 10;
    static constexpr float kFadeSamples = 240.f; // 5 ms
    static constexpr float kVoiceGain   = 0.55f;
    static constexpr float kCrossRecord = 0.12f; // buffer interaction
    static constexpr float kLinkProb    = 0.30f; // follow the partner voice
    static constexpr float kRefreshProb = 0.25f; // re-grab the newest audio
    static constexpr float kLenDrift    = 0.4f;  // octaves of random size drift
    static constexpr int   kGuard       = 480;

    struct Voice
    {
        float* buf    = nullptr;
        int    id     = 0;
        int    delay  = 0;
        bool   active = false;
        bool   stagger = false; /**< wait half a slice before the next one */
        float  anchor = -1.f; // absolute buffer index of the region start
        float  s_len  = 0.f;  // slice source length, samples
        float  rate   = 1.f;
        float  dir    = 1.f;
        int    dur    = 0; // slice output length, samples
        int    m      = 0; // output samples elapsed in the slice
        float  pan_l  = 0.7f, pan_r = 0.7f;
        // two overlapping micro-grains
        float g_pos[2] = {0.f, 0.f};
        int   g_n[2]   = {0, 0};
        bool  g_on[2]  = {false, false};
    };

    // --- small helpers ---------------------------------------------------
    float Rnd()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return (rng_ >> 8) * (1.f / 16777216.f);
    }

    static float Wrap(float p)
    {
        const float n = (float)kBufLen;
        while(p >= n)
            p -= n;
        while(p < 0.f)
            p += n;
        return p;
    }

    float Read(const float* b, float p) const
    {
        p        = Wrap(p);
        const int   i0 = (int)p;
        const float f  = p - i0;
        const int   i1 = i0 + 1 >= (int)kBufLen ? 0 : i0 + 1;
        return b[i0] + f * (b[i1] - b[i0]);
    }

    float LenSamples() const
    {
        const float lo = 0.03f * sr_, hi = 3.0f * sr_;
        return lo * powf(hi / lo, (float)len_ / kLenMax);
    }

    // Source cursor for output sample m of the current slice.
    float Cursor(const Voice& v, int m) const
    {
        if(v.dir > 0.f)
            return v.anchor + kH + v.rate * m;
        return v.anchor + kW + kH + v.s_len - v.rate * m;
    }

    // --- slice scheduling ------------------------------------------------
    void StartSlice(Voice& v)
    {
        const int   p = main_, a = p < 0 ? -p : p;
        const float pf = fminf(fmaxf(0.5f + 0.25f * p, 0.f), 1.f);
        v.dir          = Rnd() < pf ? 1.f : -1.f;

        // Slow (half speed, same pitch) slices.
        float slow_p = 0.f;
        if(a > 12)
            slow_p = 1.f;
        else if(a > 5)
            slow_p = (a - 5) / 8.f;
        v.rate = 1.f;
        if(Rnd() < slow_p)
            v.rate = a <= 12 ? 0.5f : 0.5f - 0.15f * (a - 12) / 6.f;

        // Slice size, with a little natural drift.
        float s = LenSamples() * powf(2.f, (Rnd() * 2.f - 1.f) * kLenDrift);
        s       = fminf(fmaxf(s, 0.02f * sr_), 3.2f * sr_);
        v.s_len = s;
        v.dur   = (int)(s / v.rate);
        v.m     = 0;

        // Choose where to grab from.
        const float region = s + 2.f * kW;
        const float min_age = region + kGuard;
        const float max_age = (float)kBufLen - kGuard;
        float       age;
        const Voice& partner = voice_[(v.id + 2) % kVoices];
        const float  cur_age = v.anchor < 0.f ? -1.f : Wrap((float)wi_ - v.anchor);

        if(filled_ < (int)min_age)
            age = min_age; // buffer still filling: newest audio
        else if(partner.anchor >= 0.f && Rnd() < kLinkProb)
        {
            age = Wrap((float)wi_ - partner.anchor) + (Rnd() * 2.f - 1.f) * 1000.f;
        }
        else if(Rnd() < (float)a / kMainMax)
            age = min_age + Rnd() * (max_age - min_age);
        else if(cur_age >= min_age && cur_age <= max_age && Rnd() >= kRefreshProb)
            age = cur_age;
        else
            age = min_age;
        age = fminf(fmaxf(age, min_age), max_age);
        v.anchor = Wrap((float)wi_ - age);

        // Random fixed stereo spot; each buffer leans to its own side.
        const float pan = (v.id < 2 ? -1.f : -0.5f) + Rnd() * 1.5f;
        const float ang = (pan + 1.f) * 0.7853982f;
        v.pan_l         = cosf(ang);
        v.pan_r         = sinf(ang);

        // First micro-grain starts at its peak so the slice begins at full level.
        v.g_n[0]  = kH;
        v.g_pos[0] = Cursor(v, 0) - v.dir * kH;
        v.g_on[0] = true;
        v.g_on[1] = false;
        v.active  = true;
        flash_    = true;
    }

    float Tick(Voice& v)
    {
        if(v.delay > 0)
        {
            v.delay--;
            return 0.f;
        }
        if(!v.active)
        {
            if(v.stagger)
            {
                v.stagger = false;
                v.delay   = (int)(LenSamples() * 0.5f);
                return 0.f;
            }
            StartSlice(v);
        }

        // Spawn the next micro-grain every hop.
        if(v.m > 0 && v.m % kH == 0)
        {
            const int slot  = (v.m / kH) & 1;
            v.g_pos[slot] = Cursor(v, v.m);
            v.g_n[slot]   = 0;
            v.g_on[slot]  = true;
        }

        float out = 0.f;
        for(int g = 0; g < 2; g++)
        {
            if(!v.g_on[g])
                continue;
            const float pos = v.g_pos[g] + v.dir * v.g_n[g];
            out += hann_[v.g_n[g]] * Read(v.buf, pos);
            if(++v.g_n[g] >= kW)
                v.g_on[g] = false;
        }

        // 5 ms raised-cosine fades at the slice edges.
        const float fade = fminf(kFadeSamples, v.dur * 0.5f);
        float       env  = 1.f;
        if(v.m < fade)
            env = 0.5f - 0.5f * cosf(3.1415927f * v.m / fade);
        else if(v.dur - v.m < fade)
            env = 0.5f - 0.5f * cosf(3.1415927f * (v.dur - v.m) / fade);

        if(++v.m >= v.dur)
            v.active = false;
        return out * env;
    }

    float    sr_ = 48000.f;
    int      wi_ = 0, filled_ = 0;
    int      main_ = 0, len_ = kDefaultLen;
    bool     flash_ = false;
    uint32_t rng_   = 0x1234abcd;
    float    hann_[kW];
    Voice    voice_[kVoices];

    static float DSY_SDRAM_BSS buf_[2][kBufLen];
};
} // namespace djin
