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

    if(failures == 0)
        std::printf("all checks passed\n");
    else
        std::printf("%d check(s) failed\n", failures);

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
