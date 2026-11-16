// Host-side checks for Boonta Elements.
//
// Upstream's DSP is not what is under test here: it has run in a few
// thousand modules for a decade. What is under test is the part that was
// written for the pedal -- the model, and the layer in Effect that turns it
// into a patch, a note and a gate -- and the one way that layer can hurt the
// DSP, which is by handing it a value outside the range the module's panel
// could ever produce. So: the model's rules, then a sweep of every knob and
// switch with every sample checked, then the input-as-trigger logic.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Effect.h"
#include "PedalState.h"

#include "PitchTracker.h"
#include "elements/dsp/part.h"

namespace
{
constexpr size_t kBlock           = 32;   // what the firmware runs
constexpr int    kBlocksPerSecond = 1000; // 32 kHz / 32

int failures = 0;

void Check(bool ok, const char* what)
{
    if(!ok)
    {
        std::printf("FAIL  %s\n", what);
        failures++;
    }
}

struct Stats
{
    float peak   = 0.f;
    bool  finite = true;
};

/** A pedal: the model, the effect, and the storage the firmware would have
 *  taken from the heap. */
struct Rig
{
    PedalState      state;
    Effect          fx;
    elements::Part* part;
    uint16_t*       reverb;

    Rig()
    {
        part = static_cast<elements::Part*>(
            std::calloc(1, sizeof(elements::Part)));
        reverb = static_cast<uint16_t*>(std::calloc(32768, sizeof(uint16_t)));
        uint32_t seed[3] = {1, 2, 3};
        fx.Init(part, reverb, seed, 3);
        state.SetBypass(false);
    }
    ~Rig()
    {
        std::free(part);
        std::free(reverb);
    }

    /** Run `blocks` blocks, `gen(n)` supplying input sample n. */
    template <typename Gen>
    Stats Run(int blocks, Gen gen)
    {
        Stats  stats;
        float  l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        size_t n = 0;

        for(int b = 0; b < blocks; b++)
        {
            for(size_t i = 0; i < kBlock; i++)
                l[i] = r[i] = gen(n++);

            const float* in[2]  = {l, r};
            float*       out[2] = {ol, orr};
            fx.Process(state, in, out, kBlock);

            for(size_t i = 0; i < kBlock; i++)
            {
                if(!std::isfinite(ol[i]) || !std::isfinite(orr[i]))
                    stats.finite = false;
                stats.peak = std::fmax(stats.peak, std::fabs(ol[i]));
                stats.peak = std::fmax(stats.peak, std::fabs(orr[i]));
            }
        }
        return stats;
    }

    Stats RunSilence(int blocks)
    {
        return Run(blocks, [](size_t) { return 0.f; });
    }
};

float Silence(size_t)
{
    return 0.f;
}

/** A plucked-string stand-in: a 196 Hz tone that starts abruptly every
 *  `period` samples and decays. */
struct Plucks
{
    size_t period;
    float  operator()(size_t n) const
    {
        const size_t k = n % period;
        const float  t = static_cast<float>(k) / 32000.f;
        return 0.3f * std::exp(-t * 6.f) * std::sin(6.2831853f * 196.f * t);
    }
};

// --- the model --------------------------------------------------------------

void TestModel()
{
    PedalState s;

    Check(s.IsBypassed(), "boots bypassed");
    Check(s.GetPage() == 0, "boots on page 0");
    Check(!s.Gate(), "boots with the gate down");

    s.PrevPage();
    Check(s.GetPage() == PedalState::kPageCount - 1, "page wraps backwards");
    s.NextPage();
    Check(s.GetPage() == 0, "page wraps forwards");

    // Notes: last note wins, the gate follows the last key up, and a
    // velocity-zero note-on is a note-off.
    s.NoteOn(60, 100);
    s.NoteOn(67, 50);
    Check(s.Gate(), "a held key raises the gate");
    Check(s.NoteOffset() == 7.f, "the last key sets the pitch");
    s.NoteOff(67);
    Check(s.Gate(), "one key still down keeps the gate");
    s.NoteOn(60, 0);
    Check(!s.Gate(), "velocity zero releases the last key");
    Check(s.NoteOffset() == 7.f, "the pitch outlives the key");
    Check(s.Velocity() == 1.f, "no key down means full velocity");
    s.NoteOff(60);
    s.NoteOff(60);
    s.NoteOn(62, 127);
    Check(s.Gate(), "spare note-offs do not leave a debt");
    s.AllNotesOff();
    Check(!s.Gate(), "all notes off");

    // The three gate sources are independent.
    s.SetGateRemote(true);
    s.SetGate(false);
    Check(s.Gate(), "the footswitch cannot cancel a remote gate");
    s.SetGateRemote(false);

    // Saved state round-trips, and rubbish is refused whole.
    PedalState a;
    a.SetParam(PedalState::PARAM_FINE, 0.25f);
    a.SetToggle(PedalState::TOGGLE_MODEL, PedalState::POS_HIGH);
    a.SetPage(2);
    const PedalState::Saved saved = a.Capture();

    PedalState b;
    Check(b.Restore(saved), "restore accepts a capture");
    Check(b.GetParam(PedalState::PARAM_FINE) == 0.25f, "restore: param");
    Check(b.GetToggle(PedalState::TOGGLE_MODEL) == PedalState::POS_HIGH,
          "restore: toggle");
    Check(b.GetPage() == 2, "restore: page");
    Check(b.IsBypassed(), "restore does not un-bypass");

    PedalState        c;
    PedalState::Saved bad = saved;
    bad.magic             = 0x434C4F55u; // Clouds' settings, same flash sector
    Check(!c.Restore(bad), "restore refuses another firmware's settings");
    bad          = saved;
    bad.param[3] = std::nanf("");
    Check(!c.Restore(bad), "restore refuses NaN");
    bad      = saved;
    bad.page = 3;
    Check(!c.Restore(bad), "restore refuses a page that does not exist");
    Check(c.GetPage() == 0, "a refused restore changes nothing");
}

// --- the effect -------------------------------------------------------------

void TestDefaultsMakeASound()
{
    // Twelve of the eighteen parameters are not under the knobs at boot, so
    // the defaults have to be a playable patch by themselves.
    Rig rig;
    rig.state.SetToggle(PedalState::TOGGLE_TRIGGER, PedalState::TRIGGER_MANUAL);
    rig.state.SetToggle(PedalState::TOGGLE_MODEL, PedalState::MODEL_MODAL);

    Stats quiet = rig.RunSilence(200);
    Check(quiet.peak < 1e-3f, "silent with the gate down and no input");

    rig.state.SetGate(true);
    Stats struck = rig.RunSilence(200);
    Check(struck.finite, "default patch: finite");
    Check(struck.peak > 0.01f, "default patch: the footswitch plays a note");
    Check(rig.state.IsSounding(), "sounding telemetry follows the gate");
}

void TestBypassRendersNothing()
{
    Rig rig;
    rig.state.SetGate(true);
    rig.RunSilence(50);
    rig.state.SetBypass(true);
    Stats s = rig.RunSilence(10);
    Check(s.peak == 0.f, "bypassed: output is exactly zero");
    Check(!rig.state.IsSounding(), "bypassed: not sounding");
}

void TestSweep()
{
    // Every parameter at both ends and the middle, one at a time over a
    // patch with everything else also at that extreme, in every model and
    // every routing, with the input driving it hard. The DSP limits its own
    // output; what this is looking for is the mapping layer handing it an
    // index one past the end of a table.
    static const float                 kEnds[] = {0.f, 0.5f, 1.f};
    static const PedalState::TogglePos kPos[]
        = {PedalState::POS_LOW, PedalState::POS_MID, PedalState::POS_HIGH};

    bool  finite = true;
    float peak   = 0.f;
    int   cases  = 0;

    for(PedalState::TogglePos model : kPos)
        for(PedalState::TogglePos route : kPos)
            for(float base : kEnds)
                for(int p = 0; p < PedalState::PARAM_LAST; p++)
                    for(float v : kEnds)
                    {
                        if(v == base)
                            continue;

                        Rig rig;
                        for(int q = 0; q < PedalState::PARAM_LAST; q++)
                            rig.state.SetParam(
                                static_cast<PedalState::Param>(q), base);
                        rig.state.SetParam(static_cast<PedalState::Param>(p),
                                           v);
                        rig.state.SetToggle(PedalState::TOGGLE_MODEL, model);
                        rig.state.SetToggle(PedalState::TOGGLE_ROUTE, route);
                        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                                            PedalState::TRIGGER_INPUT);
                        rig.state.NoteOn(127, 127); // as high as it goes
                        rig.state.SetBend(1.f);

                        Stats s = rig.Run(60, Plucks{4000});
                        finite  = finite && s.finite;
                        peak    = std::fmax(peak, s.peak);
                        cases++;
                    }

    std::printf("sweep: %d cases, worst peak %.3f\n", cases, peak);
    Check(finite, "sweep: every sample finite");
    Check(peak <= 1.0f, "sweep: output never exceeds full scale");
}

void TestLowestNote()
{
    Rig rig;
    for(int q = 0; q < PedalState::PARAM_LAST; q++)
        rig.state.SetParam(static_cast<PedalState::Param>(q), 0.f);
    rig.state.SetParam(PedalState::PARAM_STRIKE, 0.8f);
    rig.state.NoteOn(0, 127);
    rig.state.SetBend(-1.f);
    Stats s = rig.RunSilence(300);
    Check(s.finite, "lowest note: finite");
}

void TestPitchKnobIsQuantised()
{
    // A knob resting between two semitones must not make the pitch wander:
    // nudging it by less than the hysteresis leaves the note where it was.
    // There is no way to read the note back out of the Part, so this checks
    // the consequence -- the two renders are identical.
    auto render = [](float knob) {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_MANUAL);
        rig.state.SetParam(PedalState::PARAM_PITCH, 0.5f);
        rig.RunSilence(400); // let the smoothing land on 0.5
        rig.state.SetParam(PedalState::PARAM_PITCH, knob);
        rig.RunSilence(400);
        rig.state.SetGate(true);
        double sum = 0.0;
        rig.Run(100, [&](size_t) { return 0.f; });
        // Fingerprint the next stretch of output.
        float        l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        const float* in[2]  = {l, r};
        float*       out[2] = {ol, orr};
        for(size_t i = 0; i < kBlock; i++)
            l[i] = r[i] = 0.f;
        for(int b = 0; b < 50; b++)
        {
            rig.fx.Process(rig.state, in, out, kBlock);
            for(size_t i = 0; i < kBlock; i++)
                sum += std::fabs(ol[i]) * (1 + (i % 7));
        }
        return sum;
    };

    const double centre = render(0.5f);
    const double nudged = render(0.5f + 0.2f / 48.f); // a fifth of a semitone
    const double moved  = render(0.5f + 2.0f / 48.f); // two semitones
    Check(centre > 0.0, "pitch test rendered something");
    Check(centre == nudged, "a fifth of a semitone of knob does not repitch");
    Check(centre != moved, "two semitones of knob does");
}

void TestInputTrigger()
{
    // TRIGGER_MANUAL: the input excites the resonator but plays no note.
    {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_MANUAL);
        rig.Run(300, Plucks{8000});
        Check(!rig.state.IsSounding(), "manual: the input does not gate");
    }

    // TRIGGER_INPUT: an attack raises the gate, and silence drops it.
    {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_INPUT);
        rig.RunSilence(100);
        Check(!rig.state.IsSounding(), "input trigger: quiet, gate down");

        rig.Run(20, Plucks{1u << 30}); // one pluck, 20 ms in
        Check(rig.state.IsSounding(), "input trigger: an attack raises it");

        rig.RunSilence(1 * kBlocksPerSecond);
        Check(!rig.state.IsSounding(), "input trigger: silence drops it");
    }

    // A second attack over the tail of the first has to produce an edge, or
    // the exciters never fire again. Count the gate's rising edges across a
    // run of plucks that never decay to the closing threshold in between.
    {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_INPUT);

        Plucks plucks{6400}; // five a second: 200 ms apart, tau 167 ms
        size_t n      = 0;
        int    rises  = 0;
        bool   was_up = false;

        float        l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        const float* in[2]  = {l, r};
        float*       out[2] = {ol, orr};

        // One block at a time is as fine as the telemetry gets; a retrigger
        // holds the gate low for 2 ms, so every one of them is visible.
        for(int b = 0; b < 2 * kBlocksPerSecond; b++)
        {
            for(size_t i = 0; i < kBlock; i++)
                l[i] = r[i] = plucks(n++);
            rig.fx.Process(rig.state, in, out, kBlock);

            const bool up = rig.state.IsSounding();
            if(up && !was_up)
                rises++;
            was_up = up;
        }

        std::printf("input trigger: %d gate edges for 10 plucks\n", rises);
        Check(rises >= 9 && rises <= 11, "one gate edge per pluck");
    }

    // TRIGGER_DRONE: up regardless.
    {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_DRONE);
        rig.RunSilence(10);
        Check(rig.state.IsSounding(), "drone: the gate is simply up");
    }
}

void TestInputKnobAtZeroIsSilent()
{
    Rig rig;
    rig.state.SetToggle(PedalState::TOGGLE_TRIGGER, PedalState::TRIGGER_MANUAL);
    rig.state.SetParam(PedalState::PARAM_INPUT, 0.f);
    rig.RunSilence(400); // let the smoothed gain reach zero
    Stats s = rig.Run(300, Plucks{8000});
    Check(s.peak < 1e-3f, "INPUT at zero: the jack reaches nothing");
}


// --- the pitch tracker --------------------------------------------------------

/** A guitar-ish note: a sawtooth-like series whose fundamental is weaker than
 *  its second and third harmonics -- the case that makes a naive tracker
 *  report an octave up -- decaying from a pluck. */
float GuitarSample(float hz, size_t n, float decay_per_s = 3.f)
{
    const float        t   = static_cast<float>(n) / 32000.f;
    const float        amp = std::exp(-t * decay_per_s);
    static const float h[]
        = {0.5f, 1.0f, 0.8f, 0.5f, 0.35f, 0.25f, 0.15f, 0.1f};
    float v = 0.f;
    for(int k = 0; k < 8; k++)
    {
        const float f = hz * static_cast<float>(k + 1);
        if(f < 15000.f)
            v += h[k] * std::sin(6.2831853f * f * t);
    }
    return 0.15f * amp * v;
}

int Settle(PitchTracker& tr, float hz, int ms)
{
    float  buf[32];
    size_t n = 0;
    for(int b = 0; b < ms; b++)
    {
        for(size_t i = 0; i < 32; i++)
            buf[i] = GuitarSample(hz, n++, 1.f);
        tr.Process(buf, 32);
    }
    return tr.Note();
}

void TestPitchTracker()
{
    // Every open string and a spread above them, up the neck. Within a
    // semitone is what the pedal needs, and the notes here are whole ones, so
    // it has to land exactly.
    struct
    {
        float       hz;
        int         midi;
        const char* name;
    } notes[] = {{82.41f, 40, "E2"},
                 {110.0f, 45, "A2"},
                 {146.83f, 50, "D3"},
                 {196.0f, 55, "G3"},
                 {246.94f, 59, "B3"},
                 {329.63f, 64, "E4"},
                 {440.0f, 69, "A4"},
                 {587.33f, 74, "D5"},
                 {783.99f, 79, "G5"},
                 {987.77f, 83, "B5"}};

    for(const auto& n : notes)
    {
        PitchTracker tr;
        const int    got = Settle(tr, n.hz, 150);
        char         msg[64];
        std::snprintf(msg,
                      sizeof msg,
                      "tracker: %s -> %d (want %d)",
                      n.name,
                      got,
                      n.midi);
        Check(got == n.midi, msg);
        Check(tr.Locked(), "tracker: locked on a clean note");
    }

    // Silence and noise are not notes: it neither locks nor moves.
    {
        PitchTracker tr;
        Settle(tr, 196.f, 150);
        const int held = tr.Note();
        float     buf[32];
        uint32_t  rng = 12345;
        for(int b = 0; b < 300; b++)
        {
            for(float& v : buf)
            {
                rng = rng * 1664525u + 1013904223u;
                v   = 0.05f * (static_cast<float>(rng >> 8) / 8388608.f - 1.f);
            }
            tr.Process(buf, 32);
        }
        Check(tr.Note() == held, "tracker: white noise does not move the note");

        for(float& v : buf)
            v = 0.f;
        for(int b = 0; b < 100; b++)
            tr.Process(buf, 32);
        Check(!tr.Locked(), "tracker: silence is not locked");
        Check(tr.Note() == held, "tracker: silence holds the last note");
    }

    // Vibrato of +/- 40 cents must not change the note.
    {
        PitchTracker tr;
        float        buf[32];
        size_t       n     = 0;
        double       phase = 0.0;
        for(int b = 0; b < 600; b++)
        {
            for(float& v : buf)
            {
                const float t  = static_cast<float>(n++) / 32000.f;
                const float hz = 220.f
                                 * std::exp2(0.4f / 12.f
                                             * std::sin(6.2831853f * 5.5f * t));
                phase += hz / 32000.0;
                v = 0.2f
                        * std::sin(6.2831853f
                                   * static_cast<float>(std::fmod(phase, 1.0)))
                    + 0.15f
                          * std::sin(
                              6.2831853f
                              * static_cast<float>(std::fmod(2 * phase, 1.0)));
            }
            tr.Process(buf, 32);
        }
        Check(tr.Note() == 57, "tracker: 40 cent vibrato stays on A3");
        Check(tr.Generation() <= 2,
              "tracker: vibrato does not make it chatter");
    }

    // A change of note is followed, within a few tens of milliseconds.
    {
        PitchTracker tr;
        Settle(tr, 110.f, 150);
        const int got = Settle(tr, 164.81f, 90);
        Check(got == 52, "tracker: follows a new note within 90 ms");
    }
}

void TestFollowMode()
{
    // With the trigger toggle at centre the pedal tunes to the guitar. The
    // string model rings at a clean pitch, so the check is to run its output
    // through the tracker and see what note it is playing.
    auto played = [](float hz, int transpose_semitones) {
        Rig rig;
        rig.state.SetToggle(PedalState::TOGGLE_TRIGGER,
                            PedalState::TRIGGER_INPUT);
        rig.state.SetToggle(PedalState::TOGGLE_MODEL, PedalState::MODEL_STRING);
        rig.state.SetParam(PedalState::PARAM_STRIKE, 0.f);
        rig.state.SetParam(PedalState::PARAM_PITCH,
                           (24.f + transpose_semitones) / 48.f);
        rig.state.SetParam(PedalState::PARAM_DAMPING, 0.9f);
        // The guitar tells the pedal what to play but does not itself drive
        // the string, so what comes out is only what the resonator is tuned to.
        rig.state.SetParam(PedalState::PARAM_INPUT, 0.f);
        rig.state.SetToggle(PedalState::TOGGLE_ROUTE, PedalState::ROUTE_STRIKE);

        PitchTracker tr;
        float        l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
        const float* in[2]  = {l, r};
        float*       out[2] = {ol, orr};
        size_t       n      = 0;
        for(int b = 0; b < 400; b++)
        {
            for(size_t i = 0; i < kBlock; i++)
                l[i] = r[i] = GuitarSample(hz, n++, 2.f);
            rig.fx.Process(rig.state, in, out, kBlock);
            if(b >= 200)
                tr.Process(ol, kBlock);
        }
        return tr.Locked() ? tr.Note() : -1;
    };

    const int a2 = played(110.f, 0);
    const int a3 = played(220.f, 0);
    std::printf("follow: guitar A2 -> %d, A3 -> %d, A2 up an octave -> %d\n",
                a2,
                a3,
                played(110.f, 12));
    Check(a2 == 45, "follow: the resonator is tuned to the guitar's A2");
    Check(a3 == 57, "follow: and to its A3");
    Check(played(110.f, 12) == 57, "follow: the Pitch knob transposes");
    {
        const int dn = played(220.f, -12);
        std::printf("follow: A3 down an octave -> %d\n", dn);
        Check(dn == 45, "follow: and down (A3 guitar to A2)");
    }
}

} // namespace

int main()
{
    (void)Silence;

    TestModel();
    TestDefaultsMakeASound();
    TestBypassRendersNothing();
    TestLowestNote();
    TestPitchKnobIsQuantised();
    TestInputTrigger();
    TestInputKnobAtZeroIsSilent();
    TestPitchTracker();
    TestFollowMode();
    TestSweep();

    if(failures == 0)
        std::printf("all checks passed\n");
    else
        std::printf("%d check(s) FAILED\n", failures);

    return failures == 0 ? 0 : 1;
}
