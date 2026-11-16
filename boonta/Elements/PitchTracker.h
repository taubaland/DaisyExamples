#pragma once
#ifndef BOONTA_PITCH_TRACKER_H
#define BOONTA_PITCH_TRACKER_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/** Monophonic pitch tracker for a guitar: the McLeod pitch method.
 *
 *  Normalised square difference over a 32 ms window, first strong peak, with
 *  parabolic interpolation. It was chosen over plain autocorrelation because
 *  its normalisation makes the peak height a usable measure of *how periodic*
 *  the signal is, which is what separates a plucked string from the noise
 *  that follows it, and because picking the first peak that is nearly as tall
 *  as the tallest is what stops a guitar's strong second harmonic being
 *  reported an octave high.
 *
 *  Works at 16 kHz, on the input low-passed and halved, which is plenty for a
 *  guitar (fundamentals run from 82 Hz to about 1.2 kHz) and halves the cost.
 *
 *  Latency is the window: a new note is first seen about 30 ms after its
 *  attack, longer for the lowest strings, whose period is 12 ms.
 *
 *  Output is a whole MIDI note, deliberately. A guitar's pitch wanders with
 *  vibrato, bends and the pluck itself going sharp for the first few
 *  milliseconds, and a resonator that chases all of that sounds seasick. The
 *  note only changes once two analyses in a row agree it has moved by more
 *  than 0.65 of a semitone, which is hysteresis wide enough to sit through a
 *  full-width vibrato.
 *
 *  Knows nothing about libDaisy, so it builds and runs on a host.
 */
class PitchTracker
{
  public:
    static constexpr float  kInputRate = 32000.f;
    static constexpr float  kRate      = 16000.f; /**< after decimation */
    static constexpr size_t kWindow    = 512;
    static constexpr size_t kMinLag    = 6;   /**< about 2.7 kHz */
    static constexpr size_t kMaxLag    = 224; /**< about 71 Hz */
    static constexpr size_t kHop       = 256; /**< decimated samples, 16 ms */

    /** How periodic a window has to be to be believed. */
    static constexpr float kClarity = 0.85f;

    /** The first peak that is at least this fraction of the tallest wins. */
    static constexpr float kPeakRatio = 0.9f;

    /** Below this mean power a window is treated as silence. -50 dBFS. */
    static constexpr float kSilence = 1e-5f;

    PitchTracker() { Init(); }

    void Init()
    {
        for(size_t i = 0; i < kBuffer; i++)
            buf_[i] = 0.f;
        head_       = 0;
        since_      = 0;
        busy_       = false;
        budget_     = 0.f;
        last_in_    = 0.f;
        have_half_  = false;
        half_       = 0.f;
        locked_     = false;
        clarity_    = 0.f;
        frequency_  = 0.f;
        note_       = kStartNote;
        pending_    = kStartNote;
        generation_ = 0;
    }

    /** Feed 32 kHz samples. Any count. Analysis runs inside this call, once
     *  every 16 ms of signal, and takes a few hundred microseconds. */
    void Process(const float* in, size_t size)
    {
        for(size_t i = 0; i < size; i++)
        {
            // Pair up, and low-pass on the way: [1 2 1] / 4 across the
            // pair and its predecessor. Nothing sophisticated; what is above
            // 8 kHz on a guitar pickup is little and none of it is pitch.
            if(!have_half_)
            {
                half_      = in[i];
                have_half_ = true;
                continue;
            }
            have_half_    = false;
            const float y = 0.25f * last_in_ + 0.5f * half_ + 0.25f * in[i];
            last_in_      = in[i];

            buf_[head_] = y;
            head_       = (head_ + 1) % kBuffer;

            if(++since_ >= kHop)
            {
                since_ = 0;
                Begin();
            }
        }

        // Spend this call's share of the analysis. Done in one go it is a
        // few milliseconds, more than the whole audio block; spread over
        // three quarters of a hop it is a few tens of microseconds a call.
        if(busy_)
        {
            budget_ += static_cast<float>(size / 2) * kLagsPerSample;
            while(busy_ && budget_ >= 1.f)
            {
                budget_ -= 1.f;
                Step();
            }
        }
    }

    /** True while the last analysis found a clearly periodic signal. */
    bool Locked() const { return locked_; }

    /** The tracked note, a whole MIDI note. Holds its last value when the
     *  signal goes quiet or unpitched, so a decaying note keeps its pitch. */
    int Note() const { return note_; }

    /** Counts up each time Note() changes. */
    uint32_t Generation() const { return generation_; }

    /** The last accepted estimate, unquantised, and how sure of it. For
     *  tests and the debugger. */
    float Frequency() const { return frequency_; }
    float Clarity() const { return clarity_; }

  private:
    static constexpr size_t kBuffer = 1024; /**< power of two, > window + lag */
    static constexpr int kStartNote = 48;   /**< C3, before anything is heard */

    float At(size_t age) const // age 0 = newest
    {
        return buf_[(head_ + kBuffer - 1 - age) % kBuffer];
    }

    /** Lags to compute per analysis, and how many per decimated sample to
     *  compute them at: enough to finish in three quarters of a hop. */
    static constexpr size_t kFirstLag = 1;
    static constexpr size_t kLastLag  = kMaxLag + 1;
    static constexpr float  kLagsPerSample
        = static_cast<float>(kLastLag - kFirstLag + 1) / (0.75f * kHop);

    static constexpr size_t kSpan = kWindow + kMaxLag + 1;
    static_assert(kSpan + 1 <= kBuffer, "buffer too small for the window");

    /** Snapshot the newest samples and get ready to correlate them. */
    void Begin()
    {
        for(size_t i = 0; i < kSpan; i++)
            x_[i] = At(kSpan - 1 - i);

        // Running sum of squares, so the energy term for any lag is two
        // lookups instead of a loop. Only the correlation itself is a loop.
        sq_[0] = 0.f;
        for(size_t i = 0; i < kSpan; i++)
            sq_[i + 1] = sq_[i] + x_[i] * x_[i];

        const size_t start = kSpan - kWindow;
        energy_            = sq_[kSpan] - sq_[start];

        if(energy_ < kSilence * kWindow)
        {
            locked_ = false;
            busy_   = false;
            return;
        }

        lag_    = kFirstLag;
        busy_   = true;
        budget_ = 0.f;
    }

    /** One lag of the normalised square difference. */
    void Step()
    {
        const size_t start = kSpan - kWindow;
        const float* a     = x_ + start;
        const float* b     = a - lag_;

        float acf = 0.f;
        for(size_t j = 0; j < kWindow; j++)
            acf += a[j] * b[j];

        const float m
            = energy_ + (sq_[start - lag_ + kWindow] - sq_[start - lag_]);
        nsdf_[lag_] = m > 1e-12f ? 2.f * acf / m : 0.f;

        if(++lag_ > kLastLag)
        {
            busy_ = false;
            Pick();
        }
    }

    /** All lags are in: find the pitch. */
    void Pick()
    {
        const float* nsdf = nsdf_;

        // Peak picking. Skip the lobe that starts at lag zero, then take the
        // highest point of each positive lobe in turn.
        size_t tau = 1;
        while(tau <= kMaxLag && nsdf[tau] > 0.f)
            tau++;

        size_t peak_at[16];
        float  peak_val[16];
        int    peaks   = 0;
        float  tallest = 0.f;

        while(tau <= kMaxLag && peaks < 16)
        {
            while(tau <= kMaxLag && nsdf[tau] <= 0.f)
                tau++;
            if(tau > kMaxLag)
                break;

            size_t best = tau;
            while(tau <= kMaxLag + 1 && nsdf[tau] > 0.f)
            {
                if(nsdf[tau] > nsdf[best])
                    best = tau;
                tau++;
            }
            if(best >= kMinLag && best < kMaxLag)
            {
                peak_at[peaks]  = best;
                peak_val[peaks] = nsdf[best];
                if(nsdf[best] > tallest)
                    tallest = nsdf[best];
                peaks++;
            }
        }

        int pick = -1;
        for(int i = 0; i < peaks; i++)
            if(peak_val[i] >= kPeakRatio * tallest)
            {
                pick = i;
                break;
            }

        if(pick < 0 || peak_val[pick] < kClarity)
        {
            locked_  = false;
            clarity_ = pick < 0 ? 0.f : peak_val[pick];
            return;
        }

        // Parabolic interpolation around the peak.
        const size_t t  = peak_at[pick];
        const float  y0 = nsdf[t - 1], y1 = nsdf[t], y2 = nsdf[t + 1];
        const float  d     = y0 - 2.f * y1 + y2;
        float        shift = d != 0.f ? 0.5f * (y0 - y2) / d : 0.f;
        if(shift > 0.5f)
            shift = 0.5f;
        if(shift < -0.5f)
            shift = -0.5f;

        const float lag = static_cast<float>(t) + shift;

        locked_    = true;
        clarity_   = y1;
        frequency_ = kRate / lag;

        Accept(69.f + 12.f * log2f(frequency_ / 440.f));
    }

    void Accept(float midi)
    {
        // Within the hysteresis band of the current note: nothing to do.
        const float off = midi - static_cast<float>(note_);
        if(off < 0.65f && off > -0.65f)
        {
            pending_ = note_;
            return;
        }

        // Outside it. Believe it when two analyses in a row land on the
        // same note, which costs one more hop of latency and removes the
        // single bad estimate at the edge of a pluck.
        const int cand = static_cast<int>(midi + 0.5f);
        if(cand == pending_)
        {
            note_ = cand;
            generation_++;
        }
        pending_ = cand;
    }

    float    buf_[kBuffer];
    float    x_[kSpan];      /**< snapshot being analysed */
    float    sq_[kSpan + 1]; /**< running sum of squares of x_ */
    float    nsdf_[kMaxLag + 2];
    float    energy_;
    size_t   lag_;
    bool     busy_;
    float    budget_;
    size_t   head_;
    size_t   since_;
    float    last_in_;
    bool     have_half_;
    float    half_;
    bool     locked_;
    float    clarity_;
    float    frequency_;
    int      note_;
    int      pending_;
    uint32_t generation_;
};

#endif
