// Host-side checks for the Beads granulator.
//
// PedalState and Effect deliberately know nothing about libDaisy, so they
// build and run on the machine you are sitting at -- no board, no toolchain,
// no flashing. Controls and LedView do touch libDaisy and are not covered
// here.
//
// What these are for is the two kinds of mistake a grain engine makes that
// you cannot hear until you are standing in front of an amp: a parameter
// combination that divides by zero or indexes off the end, and a level that
// runs away. So the bulk of it is a sweep over every knob and switch, with
// every sample checked for being finite and bounded. On top of that the
// pitch path gets measured, because quantisation is the one thing here that
// is wrong in a way you would blame on your playing.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Effect.h"
#include "PedalState.h"

namespace
{
constexpr float  kSampleRate = 48000.f;
constexpr size_t kBlock      = 48;

/** Blocks of audio to run before measuring anything.
 *
 *  TIME reaches back through the buffer, and even at its shortest a grain
 *  starts one grain length plus the guard behind the write head -- so a
 *  freshly initialised granulator reads silence until that much has been
 *  recorded. Two seconds covers the longest reach any of these tests asks
 *  for. */
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

struct BlockStats
{
    float peak     = 0.f;
    float in_peak  = 0.f;
    bool  finite   = true;
};

/** Run `blocks` blocks through the effect and report what came out. `gen` is
 *  called per sample to make the input. */
template <typename Gen>
BlockStats Run(Effect&            fx,
               PedalState&        state,
               int                blocks,
               Gen                gen,
               std::vector<float>* collect = nullptr)
{
    BlockStats stats;

    float  l[kBlock], r[kBlock], ol[kBlock], or_[kBlock];
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

        float in_peak = 0.f;
        fx.Process(state, in, out, kBlock, &in_peak);
        if(in_peak > stats.in_peak)
            stats.in_peak = in_peak;

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
            if(collect != nullptr)
                collect->push_back(ol[i]);
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

/** Positive-going zero crossings in a captured signal.
 *
 *  A direct proxy for frequency, and the right tool here. Autocorrelation
 *  looks like the better instrument but is not: granular transposition by a
 *  non-octave ratio restarts the carrier phase at every grain, so the signal
 *  repeats at the beat between the grain rate and the carrier rather than at
 *  the carrier period, and the strongest correlation peak is nowhere near
 *  the pitch you hear. Crossings count the carrier regardless.
 */
int CountCrossings(const std::vector<float>& x)
{
    int   count = 0;
    float prev  = 0.f;
    for(float v : x)
    {
        if(prev <= 0.f && v > 0.f)
            count++;
        prev = v;
    }
    return count;
}

/** Settings that make the pitch measurable: no scatter, grains dense enough
 *  to be continuous, feedback off so nothing but the source is in there. */
PedalState PitchTestState()
{
    PedalState s;
    s.SetBypass(false);
    SetAll(s, 0.5f);
    s.SetParam(PedalState::PARAM_MIX, 1.f);
    s.SetParam(PedalState::PARAM_TIME, 0.2f);
    // The shortest grains, just slow enough that they do not overlap.
    // DENSITY near the top means near-regular spacing, and an overlap a
    // little under one means that for nearly all of the time exactly one
    // grain is sounding -- so the output is the transposed source rather
    // than several phase-shifted copies of it comb filtering each other.
    s.SetParam(PedalState::PARAM_SIZE, 0.f);
    s.SetParam(PedalState::PARAM_DENSITY, 0.9f);
    s.SetParam(PedalState::PARAM_SHAPE, 0.5f);
    s.SetToggle(PedalState::TOGGLE_SEED, PedalState::POS_LOW); // no scatter
    s.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);
    s.SetToggle(PedalState::TOGGLE_QUALITY, PedalState::POS_LOW); // clean
    return s;
}

int MeasureCrossings(float pitch_knob, PedalState::Quantise quant)
{
    Effect     fx;
    PedalState s = PitchTestState();
    fx.Init(kSampleRate);

    s.SetQuantise(quant);
    s.SetParam(PedalState::PARAM_PITCH, pitch_knob);

    Sine sine;
    Run(fx, s, kPrimeBlocks, sine);

    std::vector<float> captured;
    captured.reserve(kBlock * 400);
    Run(fx, s, 400, sine, &captured);

    return CountCrossings(captured);
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

    float in_peak = 0.f;
    fx.Process(state, in, out, kBlock, &in_peak);

    bool exact = true;
    for(size_t i = 0; i < kBlock; i++)
        exact = exact && ol[i] == l[i] && or_[i] == r[i];

    Check(exact, "bypass passes the input through bit for bit");
    Check(in_peak > 0.f, "the recording path still reports a level when bypassed");
}

void TestItMakesSound()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_TIME, 0.1f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.8f);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);
    const BlockStats s = Run(fx, state, 200, sine);

    Check(s.finite, "output stays finite");
    Check(s.peak > 0.01f, "the cloud makes audible output");
    Check(s.peak < 4.f, "output stays bounded");
    std::printf("      wet peak from a -6 dBFS sine: %.3f\n",
                static_cast<double>(s.peak));
}

void TestShapeIsAsymmetric()
{
    // SHAPE at the two ends should not produce the same thing. A percussive
    // grain and a reversed one have the same spectrum and the same level, so
    // what separates them is where the energy sits in time.
    //
    // Measuring that on a sine is hopeless -- the carrier swamps the
    // envelope. Feeding DC instead makes the output *be* the envelope, so
    // counting how many samples the output spends rising rather than falling
    // reads the window shape directly. DENSITY is low enough that grains
    // rarely overlap, which is the other thing that would average the shape
    // away.
    struct Dc
    {
        float operator()() const { return 0.5f; }
    };

    auto rising_fraction = [](float shape) {
        Effect     fx;
        PedalState s;
        fx.Init(kSampleRate);
        s.SetBypass(false);
        SetAll(s, 0.5f);
        s.SetParam(PedalState::PARAM_MIX, 1.f);
        s.SetParam(PedalState::PARAM_TIME, 0.1f);
        s.SetParam(PedalState::PARAM_SIZE, 0.5f);    // 0.1 s grains
        s.SetParam(PedalState::PARAM_DENSITY, 0.2f); // ~2.7 /s: isolated
        s.SetParam(PedalState::PARAM_SHAPE, shape);
        s.SetToggle(PedalState::TOGGLE_SEED, PedalState::POS_LOW);
        s.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);
        s.SetToggle(PedalState::TOGGLE_QUALITY, PedalState::POS_LOW);

        Run(fx, s, kPrimeBlocks, Dc{});

        std::vector<float> captured;
        Run(fx, s, 4000, Dc{}, &captured);

        long rising = 0, falling = 0;
        for(size_t i = 1; i < captured.size(); i++)
        {
            // Only count where a grain is actually sounding; the gaps
            // between them are flat and belong to neither.
            if(std::fabs(captured[i]) < 1e-4f)
                continue;

            if(captured[i] > captured[i - 1])
                rising++;
            else if(captured[i] < captured[i - 1])
                falling++;
        }
        return static_cast<double>(rising)
               / static_cast<double>(rising + falling + 1);
    };

    const double percussive = rising_fraction(0.02f); // all decay
    const double reversed   = rising_fraction(0.98f); // all attack

    Check(percussive < 0.25,
          "a percussive grain spends most of its time falling");
    Check(reversed > 0.75, "a reversed grain spends most of its time rising");
    std::printf("      rising fraction: percussive %.3f, reversed %.3f\n",
                percussive,
                reversed);
}

void TestFreezeHoldsMaterial()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_TIME, 0.1f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.9f);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);

    // Freeze, then feed silence. The grains should carry on reading what was
    // recorded, which is the whole point of the footswitch.
    state.SetFreeze(true);
    const BlockStats s = Run(fx, state, 400, Silence{});

    Check(s.finite, "frozen output stays finite");
    Check(s.peak > 0.01f, "freeze keeps playing after the input stops");
    Check(s.in_peak == 0.f, "freeze stops the recording path");
}

void TestUnfrozenDecaysToSilence()
{
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_TIME, 0.f); // shortest reach back
    state.SetParam(PedalState::PARAM_SIZE, 0.2f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.9f);
    state.SetToggle(PedalState::TOGGLE_SEED, PedalState::POS_LOW);
    state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_LOW);

    Sine sine;
    Run(fx, state, kPrimeBlocks, sine);

    Run(fx, state, kPrimeBlocks, Silence{});
    const BlockStats s = Run(fx, state, 200, Silence{});

    Check(s.peak < 1e-3f, "without freeze or feedback the cloud dies away");
}

void TestFeedbackDoesNotRunAway()
{
    // Everything that can add energy, turned up, for five seconds. The
    // recording path is soft clipped, so this has to settle rather than
    // climb -- and if that clip is ever removed, this is what notices.
    Effect     fx;
    PedalState state;
    fx.Init(kSampleRate);

    state.SetBypass(false);
    SetAll(state, 0.5f);
    state.SetParam(PedalState::PARAM_MIX, 1.f);
    state.SetParam(PedalState::PARAM_TIME, 0.1f);
    state.SetParam(PedalState::PARAM_DENSITY, 0.9f);
    state.SetToggle(PedalState::TOGGLE_FEEDBACK, PedalState::POS_HIGH);
    state.SetToggle(PedalState::TOGGLE_QUALITY, PedalState::POS_HIGH);
    state.SetToggle(PedalState::TOGGLE_SEED, PedalState::POS_HIGH);

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

void TestPitchAndQuantisation()
{
    // Ratios rather than absolute frequencies: the grains leave small gaps
    // between each other, so some crossings are missed -- but the same
    // proportion is missed at every pitch, which leaves the ratios exact.
    const int unity = MeasureCrossings(0.5f, PedalState::QUANT_FREE);
    const int up    = MeasureCrossings(0.75f, PedalState::QUANT_FREE);

    Check(unity > 100, "unity pitch produces a periodic signal");

    // A few percent of slack: each grain boundary is worth about one
    // crossing either way, and there are more grain boundaries per cycle of
    // the carrier an octave up, so the count comes in slightly under 2.
    const float octave = static_cast<float>(up) / static_cast<float>(unity);
    Check(octave > 1.85f && octave < 2.15f, "+1 octave doubles the pitch");

    // The pitch knob spans 48 semitones, so 0.55 is +2.4 of them -- a ratio
    // of 1.149. Free, that is an out-of-tune minor second; snapped to
    // octaves and fifths it is unity. This is the check that the quantiser
    // is wired into the grain rather than hanging off the end of the chain
    // doing nothing.
    const int free_off = MeasureCrossings(0.55f, PedalState::QUANT_FREE);
    const int snapped  = MeasureCrossings(0.55f, PedalState::QUANT_HARMONIC);

    const float free_ratio
        = static_cast<float>(free_off) / static_cast<float>(unity);
    const float snap_ratio
        = static_cast<float>(snapped) / static_cast<float>(unity);

    Check(free_ratio > 1.10f && free_ratio < 1.20f,
          "unquantised, +2.4 semitones raises the pitch by 1.149x");
    Check(snap_ratio > 0.98f && snap_ratio < 1.02f,
          "harmonic quantisation snaps +2.4 semitones back to unity");

    std::printf("      crossings: unity %d, +1 oct %d (%.3fx)\n",
                unity,
                up,
                static_cast<double>(octave));
    std::printf("      +2.4 semitones: free %.3fx, harmonic %.3fx\n",
                static_cast<double>(free_ratio),
                static_cast<double>(snap_ratio));
}

void TestEveryKnobAndSwitch()
{
    // The real test: walk every quantisation, every toggle position and the
    // ends and middle of every knob, and demand that nothing ever goes
    // non-finite or past a sane ceiling. This is what catches a divide by
    // zero in a window or a read index that walked off the buffer.
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

    for(int q = 0; q < PedalState::kQuantiseCount; q++)
    {
        state.SetQuantise(q);

        for(int t = 0; t < 3; t++)
        {
            const auto pos = static_cast<PedalState::TogglePos>(t);
            state.SetToggle(PedalState::TOGGLE_SEED, pos);
            state.SetToggle(PedalState::TOGGLE_FEEDBACK, pos);
            state.SetToggle(PedalState::TOGGLE_QUALITY, pos);

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
} // namespace

int main()
{
    TestBypassIsExact();
    TestItMakesSound();
    TestShapeIsAsymmetric();
    TestFreezeHoldsMaterial();
    TestUnfrozenDecaysToSilence();
    TestFeedbackDoesNotRunAway();
    TestPitchAndQuantisation();
    TestEveryKnobAndSwitch();

    if(failures == 0)
        std::printf("all checks passed\n");
    else
        std::printf("%d check(s) failed\n", failures);

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
