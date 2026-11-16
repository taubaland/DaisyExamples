// Host-side checks for the Clouds granulator.
//
// PedalState, Effect and Reverb deliberately know nothing about libDaisy, so
// they build and run on the machine you are sitting at -- no board, no
// toolchain, no flashing. Controls and LedView do touch libDaisy and are not
// covered here.
//
// What these are for is the two kinds of mistake a grain engine makes that
// you cannot hear until you are standing in front of an amp: a parameter
// combination that divides by zero or indexes off the end, and a level that
// runs away. So the bulk of it is a sweep over every knob and switch, with
// every sample checked for being finite and bounded.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Effect.h"
#include "PedalState.h"

namespace
{
constexpr float  kSampleRate = 48000.f;
constexpr size_t kBlock      = 48;

/** Blocks of audio to run before measuring anything.
 *
 *  POSITION reaches back through the buffer, and even at zero a grain starts
 *  one grain length plus the guard behind the write head -- so a freshly
 *  initialised granulator reads silence until that much has been recorded.
 *  Two seconds covers the longest reach any of these tests asks for. */
constexpr int kPrimeBlocks = 2000;

int failures = 0;

void Check(bool ok, const char* what)
{
    if(!ok)
    {
        std::printf("FAIL  %s\n", what);
        failures++;
    }
}

/** Peak of a block, and a NaN/inf check in the same pass. */
struct BlockStats
{
    float peak   = 0.f;
    bool  finite = true;
};

/** Run `blocks` blocks of `in_l`/`in_r` through the effect and report what
 *  came out. `gen` is called per sample to make the input. */
template <typename Gen>
BlockStats Run(Effect& fx, PedalState& state, int blocks, Gen gen)
{
    BlockStats stats;

    float  l[kBlock], r[kBlock];
    float  ol[kBlock], or_[kBlock];
    float* in[2]  = {l, r};
    float* out[2] = {ol, or_};

    for(int b = 0; b < blocks; b++)
    {
        for(size_t i = 0; i < kBlock; i++)
        {
            const float x = gen();
            l[i]          = x;
            r[i]          = x;
        }

        fx.Process(state, in, out, kBlock);

        for(size_t i = 0; i < kBlock; i++)
        {
            const float v[2] = {ol[i], or_[i]};
            for(int c = 0; c < 2; c++)
            {
                if(!std::isfinite(v[c]))
                    stats.finite = false;
                const float a = std::fabs(v[c]);
                if(a > stats.peak)
                    stats.peak = a;
            }
        }
    }

    return stats;
}

/** A 440 Hz sine at -6 dBFS, which is about what a hot pickup gives. */
struct Sine
{
    float phase = 0.f;
    float operator()()
    {
        const float x = 0.5f * std::sin(phase);
        phase += 2.f * 3.14159265f * 440.f / kSampleRate;
        if(phase > 2.f * 3.14159265f)
            phase -= 2.f * 3.14159265f;
        return x;
    }
};

struct Silence
{
    float operator()() const { return 0.f; }
};

void SetAll(PedalState& s, float value)
{
    for(int i = 0; i < PedalState::PARAM_LAST; i++)
        s.SetParam(static_cast<PedalState::Param>(i), value);
}

// ---------------------------------------------------------------------------

void TestBypassIsExact()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(true);
    SetAll(state, 0.5f);

    float  l[kBlock], r[kBlock], ol[kBlock], or_[kBlock];
    float* in[2]  = {l, r};
    float* out[2] = {ol, or_};

    Sine sine;
    for(size_t i = 0; i < kBlock; i++)
    {
        l[i] = sine();
        r[i] = -l[i];
    }

    fx.Process(state, in, out, kBlock);

    bool exact = true;
    for(size_t i = 0; i < kBlock; i++)
        exact = exact && ol[i] == l[i] && or_[i] == r[i];

    Check(exact, "bypass passes the input through bit for bit");
}

void TestItMakesSound()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    state.SetMode(PedalState::MODE_GRANULAR);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f); // wet only
    state.SetParam(PedalState::PARAM_DENSITY, 0.8f);
    state.SetParam(PedalState::PARAM_POSITION, 0.f);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);
    const BlockStats s = Run(fx, state, 200, sine);

    Check(s.finite, "granular output stays finite");
    Check(s.peak > 0.01f, "granular mode makes audible output");
    Check(s.peak < 4.f, "granular output stays bounded");
    std::printf("      wet peak from a -6 dBFS sine: %.3f\n",
                static_cast<double>(s.peak));
}

void TestFeedbackDoesNotRunAway()
{
    // Everything that can add energy, turned up, for five seconds. The
    // feedback path is soft clipped and the reverb decay is capped, so this
    // has to settle rather than climb -- and if either of those caps is ever
    // removed, this is what notices.
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    state.SetMode(PedalState::MODE_DELAY);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_POSITION, 0.05f);
    state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_HIGH);
    state.SetToggle(PedalState::TOGGLE_REVERB, PedalState::POS_HIGH);
    state.SetToggle(PedalState::TOGGLE_SPREAD, PedalState::POS_HIGH);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);

    const BlockStats early = Run(fx, state, 1000, sine);
    const BlockStats late  = Run(fx, state, 4000, sine);

    Check(late.finite, "maximum feedback stays finite");
    Check(late.peak < 4.f, "maximum feedback stays bounded");
    std::printf("      max feedback peak: %.3f after 1 s, %.3f after 5 s\n",
                static_cast<double>(early.peak),
                static_cast<double>(late.peak));
}

void TestFreezeHoldsMaterial()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    state.SetMode(PedalState::MODE_GRANULAR);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.9f);
    state.SetParam(PedalState::PARAM_POSITION, 0.f);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);

    // Freeze, then feed silence. The grains should carry on reading what was
    // recorded, which is the whole point of the footswitch.
    state.SetFreeze(true);
    const BlockStats s = Run(fx, state, 400, Silence{});

    Check(s.finite, "frozen output stays finite");
    Check(s.peak > 0.01f, "freeze keeps playing after the input stops");
}

void TestUnfrozenDecaysToSilence()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    state.SetMode(PedalState::MODE_GRANULAR);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.9f);
    state.SetParam(PedalState::PARAM_SIZE, 0.3f);
    state.SetParam(PedalState::PARAM_POSITION, 0.f); // newest material only
    state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);
    state.SetToggle(PedalState::TOGGLE_REVERB, PedalState::POS_LOW);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);

    // Not frozen, no feedback, position at the newest end: once the input
    // stops, the buffer fills with silence and the cloud must follow it down.
    Run(fx, state, kPrimeBlocks, Silence{});
    const BlockStats s = Run(fx, state, 200, Silence{});

    Check(s.peak < 1e-3f, "without freeze or feedback the cloud dies away");
}

void TestEveryModeAndKnob()
{
    // The real test: walk every mode, every toggle position and the ends and
    // middle of every knob, and demand that nothing ever goes non-finite or
    // past a sane ceiling. This is what catches a divide by zero in a window
    // or a read index that walked off the buffer.
    static const float kPoints[] = {0.f, 0.001f, 0.5f, 0.999f, 1.f};
    constexpr int      kN        = sizeof(kPoints) / sizeof(kPoints[0]);

    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    state.SetBypass(false);

    Sine sine;

    // Prime the buffer first, otherwise most of the sweep is grains reading
    // silence and the levels it reports mean nothing.
    SetAll(state, 0.5f);
    Run(fx, state, kPrimeBlocks, sine);

    float worst  = 0.f;
    bool  finite = true;

    for(int mode = 0; mode < PedalState::kModeCount; mode++)
    {
        state.SetMode(mode);

        for(int t = 0; t < 3; t++)
        {
            const auto pos = static_cast<PedalState::TogglePos>(t);
            state.SetToggle(PedalState::TOGGLE_SPREAD, pos);
            state.SetToggle(PedalState::TOGGLE_FEEDBACK, pos);
            state.SetToggle(PedalState::TOGGLE_REVERB, pos);

            for(int p = 0; p < PedalState::PARAM_LAST; p++)
            {
                for(int k = 0; k < kN; k++)
                {
                    SetAll(state, 0.5f);
                    state.SetParam(PedalState::PARAM_MIX, 1.f);
                    state.SetParam(static_cast<PedalState::Param>(p),
                                   kPoints[k]);

                    // Half the passes frozen, so the freeze path is swept too.
                    state.SetFreeze((k & 1) != 0);

                    const BlockStats s = Run(fx, state, 12, sine);
                    finite             = finite && s.finite;
                    if(s.peak > worst)
                        worst = s.peak;
                }
            }
        }
    }

    Check(finite, "no setting produces NaN or inf");
    Check(worst < 4.f, "no setting runs the level away");
    std::printf("      worst peak across the sweep: %.3f\n",
                static_cast<double>(worst));
}

void TestPitchTracks()
{
    // A grain read at twice the rate should come back an octave up. Measuring
    // pitch properly is out of scope here; counting zero crossings of a clean
    // sine is enough to catch an inverted or mis-scaled pitch map.
    //
    // MODE_DELAY with TEXTURE at zero is the setting that makes that easy to
    // measure: the crossfade is 2% of the grain, so for almost all of the
    // time exactly one grain is sounding and the output is not comb filtered
    // against a second copy of itself.
    PedalState base;
    base.SetBypass(false);
    base.SetMode(PedalState::MODE_DELAY);
    SetAll(base, 0.5f);
    base.SetParam(PedalState::PARAM_MIX, 1.f);
    base.SetParam(PedalState::PARAM_SIZE, 0.6f);
    base.SetParam(PedalState::PARAM_TEXTURE, 0.f);
    base.SetParam(PedalState::PARAM_POSITION, 0.f);
    base.SetToggle(PedalState::TOGGLE_SPREAD, PedalState::POS_LOW);
    base.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);
    base.SetToggle(PedalState::TOGGLE_REVERB, PedalState::POS_LOW);

    auto crossings = [&](float pitch) {
        Effect     fx;
        PedalState s = base;
        fx.Init(kSampleRate);
        s.SetParam(PedalState::PARAM_PITCH, pitch);

        Sine   sine;
        float  l[kBlock], r[kBlock], ol[kBlock], or_[kBlock];
        float* in[2]  = {l, r};
        float* out[2] = {ol, or_};

        int   count = 0;
        float prev  = 0.f;

        for(int b = 0; b < kPrimeBlocks + 500; b++)
        {
            for(size_t i = 0; i < kBlock; i++)
                l[i] = r[i] = sine();

            fx.Process(s, in, out, kBlock);

            if(b < kPrimeBlocks)
                continue;

            for(size_t i = 0; i < kBlock; i++)
            {
                if(prev <= 0.f && ol[i] > 0.f)
                    count++;
                prev = ol[i];
            }
        }
        return count;
    };

    const int unity = crossings(0.5f);  // centre knob = unity
    const int up    = crossings(0.75f); // +1 octave

    Check(unity > 100, "unity pitch produces a periodic signal");

    const float ratio = static_cast<float>(up) / static_cast<float>(unity);
    Check(ratio > 1.8f && ratio < 2.2f, "+1 octave roughly doubles the pitch");
    std::printf(
        "      zero crossings over 500 blocks: unity %d, +1 oct %d "
        "(ratio %.2f)\n",
        unity,
        up,
        static_cast<double>(ratio));
}

// --- the hidden layer and the spectral mode ---------------------------------

/** Output of a run, as RMS and zero crossings over its last `tail` blocks. */
struct Tail
{
    float rms       = 0.f;
    int   crossings = 0;
    bool  finite    = true;
    float peak      = 0.f;
};

template <typename Gen>
Tail RunTail(Effect& fx, PedalState& state, int blocks, int tail, Gen gen)
{
    Tail t;

    float  l[kBlock], r[kBlock];
    float  ol[kBlock], or_[kBlock];
    float* in[2]  = {l, r};
    float* out[2] = {ol, or_};

    double sum  = 0.0;
    long   n    = 0;
    float  prev = 0.f;

    for(int b = 0; b < blocks; b++)
    {
        for(size_t i = 0; i < kBlock; i++)
        {
            const float x = gen();
            l[i]          = x;
            r[i]          = x;
        }

        fx.Process(state, in, out, kBlock);

        for(size_t i = 0; i < kBlock; i++)
        {
            if(!std::isfinite(ol[i]) || !std::isfinite(or_[i]))
                t.finite = false;

            t.peak = std::fmax(
                t.peak, std::fmax(std::fabs(ol[i]), std::fabs(or_[i])));

            if(b >= blocks - tail)
            {
                sum += static_cast<double>(ol[i]) * static_cast<double>(ol[i]);
                n++;
                if((prev < 0.f) != (ol[i] < 0.f))
                    t.crossings++;
            }
            prev = ol[i];
        }
    }

    t.rms
        = n ? static_cast<float>(std::sqrt(sum / static_cast<double>(n))) : 0.f;
    return t;
}

struct Noise
{
    uint32_t s = 12345u;
    float    operator()()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (static_cast<float>(s >> 8) * (1.f / 16777216.f) - 0.5f) * 0.6f;
    }
};

void NeutralSpectral(PedalState& state)
{
    state.Reset();
    state.SetBypass(false);
    state.SetMode(PedalState::MODE_SPECTRAL);
    for(int p = 0; p < PedalState::PARAM_LAST; p++)
        state.SetParam(static_cast<PedalState::Param>(p), 0.5f);
    state.SetParam(PedalState::PARAM_POSITION, 0.f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);
    state.SetToggle(PedalState::TOGGLE_REVERB, PedalState::POS_LOW);
}

void TestHiddenLayerState()
{
    PedalState s;
    Check(s.GetMode() == PedalState::MODE_GRANULAR, "powers up in granular");
    Check(!s.IsAltOpen(), "hidden layer starts closed");

    s.NextMode();
    s.PrevMode();
    Check(s.GetMode() == PedalState::MODE_GRANULAR,
          "select buttons do nothing while the hidden layer is closed");

    s.ToggleAlt();
    Check(s.GetMode() == PedalState::MODE_STRETCH && s.IsAltOpen(),
          "the long hold opens the layer on the first hidden mode");

    s.NextMode();
    Check(s.GetMode() == PedalState::MODE_DELAY, "next: delay");
    s.NextMode();
    Check(s.GetMode() == PedalState::MODE_SPECTRAL, "next: spectral");
    s.NextMode();
    Check(s.GetMode() == PedalState::MODE_STRETCH,
          "next wraps over the hidden modes, never back to granular");
    s.PrevMode();
    Check(s.GetMode() == PedalState::MODE_SPECTRAL, "prev wraps the other way");

    s.ToggleAlt();
    Check(s.GetMode() == PedalState::MODE_GRANULAR && !s.IsAltOpen(),
          "the long hold again goes home");

    s.SetMode(3);
    Check(s.GetMode() == PedalState::MODE_SPECTRAL && s.IsAltOpen(),
          "MIDI can select a hidden mode directly, opening the layer");
    s.SetMode(0);
    Check(!s.IsAltOpen(), "and can close it");
}

void TestSpectralPassesAudio()
{
    // Everything at centre is meant to be neutral, so a steady tone should
    // come out as a steady tone: same pitch, comparable level.
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    NeutralSpectral(state);

    Sine       sine;
    const Tail t = RunTail(fx, state, 600, 100, sine);

    // 0.5 amplitude sine has an RMS of 0.354.
    std::printf("      neutral spectral: rms %.3f (input 0.354), %d crossings\n",
                static_cast<double>(t.rms),
                t.crossings);

    Check(t.finite, "spectral output is finite");
    Check(t.rms > 0.354f * 0.6f && t.rms < 0.354f * 1.5f,
          "neutral spectral keeps the level within a few dB");

    // 100 blocks of 48 samples at 440 Hz is about 44 cycles: 88 crossings.
    Check(t.crossings > 80 && t.crossings < 96,
          "neutral spectral keeps the pitch");
}

void TestSpectralBypassIsExact()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    NeutralSpectral(state);
    state.SetBypass(true);

    Sine   sine;
    float  l[kBlock], r[kBlock], ol[kBlock], or_[kBlock];
    float* in[2]  = {l, r};
    float* out[2] = {ol, or_};

    bool exact = true;
    for(int b = 0; b < 200; b++)
    {
        for(size_t i = 0; i < kBlock; i++)
            l[i] = r[i] = sine();
        fx.Process(state, in, out, kBlock);
        for(size_t i = 0; i < kBlock; i++)
            exact = exact && ol[i] == l[i] && or_[i] == r[i];
    }
    Check(exact, "bypass in spectral mode is bit exact");
}

void TestSpectralPitch()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    NeutralSpectral(state);
    state.SetParam(PedalState::PARAM_PITCH, 0.75f); // +1 octave

    Sine       sine;
    const Tail t = RunTail(fx, state, 600, 100, sine);

    std::printf("      spectral +1 oct: %d crossings (unity about 88)\n",
                t.crossings);
    Check(t.finite, "transposed spectral is finite");
    Check(t.crossings > 160 && t.crossings < 190,
          "pitch doubles the frequency");
    Check(t.rms > 0.05f, "and there is something there");
}

void TestSpectralFreeze()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    NeutralSpectral(state);

    Sine sine;
    RunTail(fx, state, 400, 1, sine);

    // Freeze, then take the input away entirely.
    state.SetFreeze(true);
    const Tail held = RunTail(fx, state, 400, 100, Silence());
    std::printf("      frozen, input silent: rms %.3f\n",
                static_cast<double>(held.rms));
    Check(held.finite, "frozen spectral is finite");
    Check(held.rms > 0.05f, "freeze keeps sounding after the input stops");
    Check(held.rms < 1.f, "and does not grow");

    // Unfrozen, the same silence has to die away.
    state.SetFreeze(false);
    const Tail gone = RunTail(fx, state, 400, 50, Silence());
    std::printf("      unfrozen, input silent: rms %.5f\n",
                static_cast<double>(gone.rms));
    Check(gone.rms < 0.01f, "without freeze, silence in is silence out");
}

void TestSpectralSweep()
{
    // Every knob at both ends and in the middle, with noise, which excites
    // every bin. Long enough per setting to get through the latency.
    const float kPts[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};

    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    Noise noise;
    float worst  = 0.f;
    bool  finite = true;

    for(int p = 0; p < PedalState::PARAM_LAST; p++)
    {
        for(int k = 0; k < 5; k++)
        {
            for(int frozen = 0; frozen < 2; frozen++)
            {
                NeutralSpectral(state);
                state.SetParam(static_cast<PedalState::Param>(p), kPts[k]);
                state.SetParam(PedalState::PARAM_MIX, 1.f);
                state.SetToggle(PedalState::TOGGLE_FEEDBACK,
                                PedalState::POS_HIGH);
                state.SetToggle(PedalState::TOGGLE_REVERB, PedalState::POS_MID);

                // Run unfrozen first so a frozen setting has slots to read.
                RunTail(fx, state, 100, 1, noise);
                state.SetFreeze(frozen != 0);
                const Tail t = RunTail(fx, state, 120, 1, noise);

                finite = finite && t.finite;
                worst  = std::fmax(worst, t.peak);
            }
        }
    }

    // All the knobs together, at the corners.
    for(int corner = 0; corner < 64; corner++)
    {
        NeutralSpectral(state);
        for(int p = 0; p < PedalState::PARAM_LAST; p++)
            state.SetParam(static_cast<PedalState::Param>(p),
                           ((corner >> p) & 1) ? 1.f : 0.f);
        state.SetParam(PedalState::PARAM_MIX, 1.f);
        state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_HIGH);

        const Tail t = RunTail(fx, state, 160, 1, noise);
        finite       = finite && t.finite;
        worst        = std::fmax(worst, t.peak);
    }

    std::printf("      worst spectral peak across the sweep: %.3f\n",
                static_cast<double>(worst));
    Check(finite, "no spectral setting produces NaN or inf");
    Check(worst < 8.f, "no spectral setting runs the level away");
}

void TestSpectralBigBlocks()
{
    // A block larger than a hop must still keep up: Service() does more
    // stages per call.
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);
    NeutralSpectral(state);

    constexpr size_t kBig = 480;
    float            l[kBig], r[kBig], ol[kBig], or_[kBig];
    float*           in[2]  = {l, r};
    float*           out[2] = {ol, or_};

    Sine   sine;
    double sum = 0.0;
    long   n   = 0;
    for(int b = 0; b < 80; b++)
    {
        for(size_t i = 0; i < kBig; i++)
            l[i] = r[i] = sine();
        fx.Process(state, in, out, kBig);
        if(b >= 40)
            for(size_t i = 0; i < kBig; i++)
            {
                sum += static_cast<double>(ol[i]) * static_cast<double>(ol[i]);
                n++;
            }
    }
    const float rms
        = static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
    Check(rms > 0.354f * 0.6f && rms < 0.354f * 1.5f,
          "spectral keeps up with blocks bigger than a hop");
}

// --- what survives a power cycle ---------------------------------------------

void TestSavedStateRoundTrip()
{
    PedalState a;
    a.SetParam(PedalState::PARAM_MIX, 0.8f);
    a.SetParam(PedalState::PARAM_PITCH, 0.25f);
    a.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_HIGH);
    a.SetMode(PedalState::MODE_SPECTRAL);
    a.SetBypass(false);
    a.SetFreeze(true);

    const PedalState::Saved saved = a.Capture();

    PedalState b;
    Check(b.Restore(saved), "a captured state restores");
    Check(b.Capture() == saved, "and captures back identical");
    Check(b.GetParam(PedalState::PARAM_MIX) == 0.8f, "knob values come back");
    Check(b.GetToggle(PedalState::TOGGLE_FEEDBACK) == PedalState::POS_HIGH,
          "toggle positions come back");
    Check(b.GetMode() == PedalState::MODE_SPECTRAL, "the mode comes back");
    Check(b.IsBypassed(), "bypass is not restored: it always boots out of circuit");
    Check(!b.IsFrozen(), "freeze is not restored");
}

void TestSavedStateIsChecked()
{
    PedalState a;
    a.SetParam(PedalState::PARAM_MIX, 0.6f);
    const PedalState::Saved good = a.Capture();

    PedalState b;
    const PedalState::Saved before = b.Capture();

    // Each of these has to be refused whole, leaving the model untouched.
    PedalState::Saved bad = good;
    bad.magic ^= 1u;
    Check(!b.Restore(bad) && b.Capture() == before, "wrong magic is refused");

    bad          = good;
    bad.param[0] = 1.5f;
    Check(!b.Restore(bad) && b.Capture() == before, "a value above 1 is refused");

    bad          = good;
    bad.param[1] = -0.1f;
    Check(!b.Restore(bad) && b.Capture() == before, "a value below 0 is refused");

    bad          = good;
    bad.param[2] = std::nanf("");
    Check(!b.Restore(bad) && b.Capture() == before, "NaN is refused");

    bad           = good;
    bad.toggle[0] = 3;
    Check(!b.Restore(bad) && b.Capture() == before,
          "a toggle position that does not exist is refused");

    bad      = good;
    bad.mode = static_cast<uint8_t>(PedalState::kModeCount);
    Check(!b.Restore(bad) && b.Capture() == before,
          "a mode that does not exist is refused");

    // What erased flash looks like.
    PedalState::Saved erased;
    for(size_t i = 0; i < sizeof(erased); i++)
        reinterpret_cast<uint8_t*>(&erased)[i] = 0xFF;
    Check(!b.Restore(erased) && b.Capture() == before,
          "erased flash is refused");
}

void TestCaptureIsStable()
{
    // Persist writes when Capture() changes, so it must not change by itself.
    PedalState a;
    a.SetParam(PedalState::PARAM_SIZE, 7.f); // out of range on purpose
    Check(a.Capture() == a.Capture(), "capture is repeatable");
    Check(a.Capture().param[PedalState::PARAM_SIZE] == 1.f,
          "and clamps a stray value into range");
}
} // namespace

int main()
{
    TestBypassIsExact();
    TestItMakesSound();
    TestFeedbackDoesNotRunAway();
    TestFreezeHoldsMaterial();
    TestUnfrozenDecaysToSilence();
    TestEveryModeAndKnob();
    TestPitchTracks();
    TestSavedStateRoundTrip();
    TestSavedStateIsChecked();
    TestCaptureIsStable();
    TestHiddenLayerState();
    TestSpectralPassesAudio();
    TestSpectralBypassIsExact();
    TestSpectralPitch();
    TestSpectralFreeze();
    TestSpectralSweep();
    TestSpectralBigBlocks();

    if(failures == 0)
        std::printf("all checks passed\n");
    else
        std::printf("%d check(s) failed\n", failures);

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
