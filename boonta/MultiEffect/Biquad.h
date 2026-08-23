#pragma once
#ifndef BOONTA_BIQUAD_H
#define BOONTA_BIQUAD_H

#include <math.h>

/** M_PI is a POSIX extension rather than standard C or C++. The ARM toolchain's
 *  newlib happens to define it, but a host compiler building these files for a
 *  test does not have to, so carry our own. */
static constexpr float kBiquadTwoPi = 6.28318530718f;

/** Second order sections, RBJ cookbook coefficients.
 *
 *  Coefficients and state are separate types on purpose. The two audio channels
 *  are always filtered identically, so a stereo band is one BiquadCoeffs -- and
 *  one round of sinf/cosf/powf per block -- shared by two BiquadStates, rather
 *  than designing the same filter twice.
 *
 *  Transposed direct form II: two state words per section, and the form that
 *  behaves best in single precision when coefficients are being nudged every
 *  block by a moving knob.
 */
struct BiquadCoeffs
{
    float b0, b1, b2, a1, a2;

    void SetIdentity()
    {
        b0 = 1.f;
        b1 = b2 = a1 = a2 = 0.f;
    }

    /** Peaking band. gain_db > 0 boosts, < 0 cuts, 0 is flat. */
    void SetPeaking(float sr, float freq, float gain_db, float q)
    {
        const float a     = powf(10.f, gain_db / 40.f);
        const float w0    = Omega(sr, freq);
        const float cs    = cosf(w0);
        const float alpha = sinf(w0) / (2.f * q);

        Normalise(1.f + alpha * a,
                  -2.f * cs,
                  1.f - alpha * a,
                  1.f + alpha / a,
                  -2.f * cs,
                  1.f - alpha / a);
    }

    /** Low shelf. `slope` of 1 is the steepest shelf that stays monotonic. */
    void SetLowShelf(float sr, float freq, float gain_db, float slope)
    {
        const float a  = powf(10.f, gain_db / 40.f);
        const float w0 = Omega(sr, freq);
        const float cs = cosf(w0);
        const float alpha
            = sinf(w0) * 0.5f * sqrtf((a + 1.f / a) * (1.f / slope - 1.f) + 2.f);
        const float beta = 2.f * sqrtf(a) * alpha;

        Normalise(a * ((a + 1.f) - (a - 1.f) * cs + beta),
                  2.f * a * ((a - 1.f) - (a + 1.f) * cs),
                  a * ((a + 1.f) - (a - 1.f) * cs - beta),
                  (a + 1.f) + (a - 1.f) * cs + beta,
                  -2.f * ((a - 1.f) + (a + 1.f) * cs),
                  (a + 1.f) + (a - 1.f) * cs - beta);
    }

    /** High shelf. */
    void SetHighShelf(float sr, float freq, float gain_db, float slope)
    {
        const float a  = powf(10.f, gain_db / 40.f);
        const float w0 = Omega(sr, freq);
        const float cs = cosf(w0);
        const float alpha
            = sinf(w0) * 0.5f * sqrtf((a + 1.f / a) * (1.f / slope - 1.f) + 2.f);
        const float beta = 2.f * sqrtf(a) * alpha;

        Normalise(a * ((a + 1.f) + (a - 1.f) * cs + beta),
                  -2.f * a * ((a - 1.f) + (a + 1.f) * cs),
                  a * ((a + 1.f) + (a - 1.f) * cs - beta),
                  (a + 1.f) - (a - 1.f) * cs + beta,
                  2.f * ((a - 1.f) - (a + 1.f) * cs),
                  (a + 1.f) - (a - 1.f) * cs - beta);
    }

  private:
    /** Angular frequency, held short of Nyquist so a knob at full travel on a
     *  high shelf cannot design an unstable section. */
    static float Omega(float sr, float freq)
    {
        const float limit = sr * 0.49f;
        if(freq > limit)
            freq = limit;
        if(freq < 1.f)
            freq = 1.f;
        return kBiquadTwoPi * freq / sr;
    }

    void Normalise(float nb0,
                   float nb1,
                   float nb2,
                   float na0,
                   float na1,
                   float na2)
    {
        const float inv = 1.f / na0;
        b0              = nb0 * inv;
        b1              = nb1 * inv;
        b2              = nb2 * inv;
        a1              = na1 * inv;
        a2              = na2 * inv;
    }
};

/** Per-channel delay memory for one section. */
struct BiquadState
{
    float z1 = 0.f, z2 = 0.f;

    void Reset() { z1 = z2 = 0.f; }

    inline float Process(const BiquadCoeffs& c, float x)
    {
        const float y = c.b0 * x + z1;
        z1            = c.b1 * x - c.a1 * y + z2;
        z2            = c.b2 * x - c.a2 * y;
        return y;
    }
};

#endif
