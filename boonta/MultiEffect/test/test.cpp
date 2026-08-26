// Host-side exercise of the Boonta MultiEffect DSP.
//
// PedalState, Chain and the three effects are deliberately free of libDaisy, so
// they build and run here. Controls and LedView are not, and are not covered.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Chain.h"
#include "PedalState.h"
#include "MidiMap.h"
#include "SavedState.h"

static int failures = 0;

static void Check(bool ok, const char* what, const char* detail = "")
{
    printf("%-6s %s %s\n", ok ? "  ok" : "FAIL", what, detail);
    if(!ok)
        failures++;
}

static constexpr float kSr    = 48000.f;
static constexpr float kTwoPi = 6.28318530718f;

// ---------------------------------------------------------------- helpers ---

struct Buffers
{
    std::vector<float> l, r, ol, orr;
    Buffers(size_t n) : l(n), r(n), ol(n), orr(n) {}
};

/** Run `frames` of a signal through the chain in 48-frame blocks, returning
 *  peak and RMS of the left output over the final `measure` frames. */
struct Result
{
    float peak, rms;
    bool  finite;
};

template <typename Gen>
static Result Run(Chain& chain, const PedalState& s, size_t frames, size_t measure, Gen gen)
{
    const size_t kBlock = 48;
    std::vector<float> il(kBlock), ir(kBlock), ol(kBlock), orr(kBlock);

    Result res{0.f, 0.f, true};
    double sumsq = 0.0;
    size_t counted = 0;

    for(size_t n = 0; n < frames; n += kBlock)
    {
        for(size_t i = 0; i < kBlock; i++)
        {
            const float v = gen(n + i);
            il[i] = v;
            ir[i] = v;
        }

        const float* in[2]  = {il.data(), ir.data()};
        float*       out[2] = {ol.data(), orr.data()};
        chain.Process(s, in, out, kBlock);

        if(n + kBlock > frames - measure)
        {
            for(size_t i = 0; i < kBlock; i++)
            {
                const float v = ol[i];
                if(!std::isfinite(v))
                    res.finite = false;
                const float a = std::fabs(v);
                if(a > res.peak)
                    res.peak = a;
                sumsq += (double)v * v;
                counted++;
            }
        }
    }

    res.rms = counted ? (float)std::sqrt(sumsq / counted) : 0.f;
    return res;
}

static float Sine(size_t n, float hz)
{
    return std::sin(kTwoPi * hz * (float)n / kSr);
}

/** A state with the chain wide open and everything neutral: EQ flat, drive and
 *  reverb fully dry, unity in and out, fully wet globally. */
static void Neutral(PedalState& s)
{
    s.Reset();
    s.SetBypass(false);
    for(int i = 0; i < PedalState::kKnobCount; i++)
        s.SetKnob(PedalState::PAGE_EQ, i, 0.5f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_IN_GAIN, 0.5f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_OUT_LEVEL, 0.5f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_MIX, 1.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_EQ_AMOUNT, 1.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 0.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_REVERB_AMOUNT, 0.0f);
}

static float Db(float ratio)
{
    return 20.f * std::log10(ratio);
}

// ------------------------------------------------------------------ tests ---

static void TestUnity()
{
    PedalState s;
    Neutral(s);

    Chain chain;
    chain.Init(kSr);

    auto r = Run(chain, s, 24000, 4800, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    char detail[128];
    snprintf(detail, sizeof detail, "(peak %.4f, expected 0.5)", r.peak);
    Check(r.finite && std::fabs(r.peak - 0.5f) < 0.01f,
          "flat EQ, dry drive and reverb passes signal at unity",
          detail);
}

static void TestEqBoostAndCut()
{
    Chain chain;
    chain.Init(kSr);

    // Reference level through the flat EQ.
    PedalState flat;
    Neutral(flat);
    auto ref = Run(chain, flat, 24000, 4800, [](size_t n) { return 0.3f * Sine(n, 60.f); });

    // Low shelf fully up, at its lowest corner so 60 Hz is well inside it.
    PedalState boost;
    Neutral(boost);
    boost.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_FREQ, 0.6f);
    boost.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_GAIN, 1.0f);

    Chain c2;
    c2.Init(kSr);
    auto up = Run(c2, boost, 24000, 4800, [](size_t n) { return 0.3f * Sine(n, 60.f); });

    PedalState cut;
    Neutral(cut);
    cut.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_FREQ, 0.6f);
    cut.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_GAIN, 0.0f);

    Chain c3;
    c3.Init(kSr);
    auto down = Run(c3, cut, 24000, 4800, [](size_t n) { return 0.3f * Sine(n, 60.f); });

    const float boost_db = Db(up.peak / ref.peak);
    const float cut_db   = Db(down.peak / ref.peak);

    char detail[160];
    snprintf(detail, sizeof detail, "(boost %+.1f dB, cut %+.1f dB, both should approach +/-15)",
             boost_db, cut_db);
    Check(boost_db > 12.f && boost_db < 16.f && cut_db < -12.f && cut_db > -16.f,
          "low shelf reaches its full +/-15 dB at 60 Hz",
          detail);

    // A band that is not being touched should leave a distant tone alone.
    Chain c4;
    c4.Init(kSr);
    auto ref_hi = Run(c4, flat, 24000, 4800, [](size_t n) { return 0.3f * Sine(n, 5000.f); });
    Chain c5;
    c5.Init(kSr);
    auto boost_hi = Run(c5, boost, 24000, 4800, [](size_t n) { return 0.3f * Sine(n, 5000.f); });

    snprintf(detail, sizeof detail, "(%.2f dB at 5 kHz)", Db(boost_hi.peak / ref_hi.peak));
    Check(std::fabs(Db(boost_hi.peak / ref_hi.peak)) < 1.0f,
          "low shelf boost leaves 5 kHz alone",
          detail);
}

static void TestDriveBounded()
{
    PedalState s;
    Neutral(s);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 1.0f);
    s.SetToggle(PedalState::TOGGLE_DRIVE_RANGE, PedalState::POS_HIGH);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_GAIN, 1.0f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_TONE, 1.0f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_LEVEL, 0.5f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_MIX, 1.0f);

    // Sweep CHARACTER across all three shapers, checking each stays bounded.
    bool  ok    = true;
    float worst = 0.f;
    for(int step = 0; step <= 10; step++)
    {
        PedalState t = s;
        t.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_CHARACTER, step / 10.f);

        Chain chain;
        chain.Init(kSr);
        auto r = Run(chain, t, 12000, 4800, [](size_t n) { return 0.9f * Sine(n, 220.f); });

        if(!r.finite || r.peak > 2.0f)
            ok = false;
        if(r.peak > worst)
            worst = r.peak;
    }

    char detail[128];
    snprintf(detail, sizeof detail, "(worst peak %.3f across the CHARACTER sweep)", worst);
    Check(ok, "drive stays bounded at full gain through soft/hard/fold", detail);
}

static void TestDriveBiasDcBlocked()
{
    PedalState s;
    Neutral(s);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 1.0f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_GAIN, 0.8f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_BIAS, 1.0f); // hard asymmetry
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_MIX, 1.0f);

    const size_t kBlock = 48, frames = 48000;
    std::vector<float> il(kBlock), ir(kBlock), ol(kBlock), orr(kBlock);

    Chain chain;
    chain.Init(kSr);

    double sum = 0.0;
    size_t counted = 0;

    for(size_t n = 0; n < frames; n += kBlock)
    {
        for(size_t i = 0; i < kBlock; i++)
        {
            il[i] = 0.5f * Sine(n + i, 220.f);
            ir[i] = il[i];
        }
        const float* in[2]  = {il.data(), ir.data()};
        float*       out[2] = {ol.data(), orr.data()};
        chain.Process(s, in, out, kBlock);

        if(n > frames / 2)
        {
            for(size_t i = 0; i < kBlock; i++)
            {
                sum += ol[i];
                counted++;
            }
        }
    }

    const float mean = (float)(sum / counted);
    char detail[128];
    snprintf(detail, sizeof detail, "(residual DC %.5f)", mean);
    Check(std::fabs(mean) < 0.01f, "full BIAS leaves no DC on the output", detail);
}

static void TestReverbDecaysAndIsStable()
{
    PedalState s;
    Neutral(s);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_REVERB_AMOUNT, 1.0f);
    s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_TIME, 1.0f);    // longest
    s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_DAMPING, 0.0f); // brightest
    s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_DIFFUSION, 1.0f);
    s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_MIX, 1.0f);
    s.SetToggle(PedalState::TOGGLE_REVERB_SIZE, PedalState::POS_HIGH); // biggest tank

    Chain chain;
    chain.Init(kSr);

    // Two seconds of loud noise-ish input to charge the tank, then ten seconds
    // of silence. Worst case for a feedback structure.
    auto charged = Run(chain, s, 96000, 4800, [](size_t n) {
        return 0.8f * (Sine(n, 220.f) + Sine(n, 317.f) + Sine(n, 941.f)) / 3.f;
    });

    auto tail = Run(chain, s, 480000, 4800, [](size_t) { return 0.f; });

    char detail[192];
    snprintf(detail, sizeof detail, "(charged rms %.4f -> after 10 s silence %.6f)",
             charged.rms, tail.rms);
    Check(charged.finite && tail.finite && charged.rms > 0.01f && tail.rms < charged.rms * 0.05f,
          "reverb tank charges, stays finite and decays at max time",
          detail);
}

static void TestReverbSizesStable()
{
    const char* names[3] = {"room", "plate", "cavern"};
    bool        ok       = true;
    char        detail[192];
    size_t      written  = 0;
    detail[0]            = 0;

    for(int pos = 0; pos < 3; pos++)
    {
        PedalState s;
        Neutral(s);
        s.SetKnob(PedalState::PAGE_META, PedalState::META_REVERB_AMOUNT, 1.0f);
        s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_TIME, 1.0f);
        s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_MIX, 1.0f);
        s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_PREDELAY, 1.0f);
        s.SetToggle(PedalState::TOGGLE_REVERB_SIZE, (PedalState::TogglePos)pos);

        Chain chain;
        chain.Init(kSr);
        auto r = Run(chain, s, 240000, 4800, [](size_t n) { return 0.7f * Sine(n, 330.f); });

        if(written < sizeof detail)
            written += snprintf(detail + written,
                                sizeof detail - written,
                                "%s %.3f ",
                                names[pos],
                                r.peak);

        if(!r.finite || r.peak > 4.f)
            ok = false;
    }

    Check(ok, "all three reverb sizes stay finite and bounded under drive", detail);
}

static void TestChainOrderMatters()
{
    // EQ boosting hard into the drive should not sound like the drive feeding
    // the same EQ boost. If the order made no difference, reordering is broken.
    PedalState a;
    Neutral(a);
    a.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 1.0f);
    a.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_GAIN, 1.0f);
    a.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_GAIN, 0.9f);
    a.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_MIX, 1.0f);

    PedalState b = a;
    // Order 0 is EQ -> Drive -> Reverb; order 2 is Drive -> EQ -> Reverb.
    b.NextOrder();
    b.NextOrder();

    Check(a.SlotAt(0) == PedalState::SLOT_EQ && a.SlotAt(1) == PedalState::SLOT_DRIVE,
          "order 0 is EQ then Drive");
    Check(b.SlotAt(0) == PedalState::SLOT_DRIVE && b.SlotAt(1) == PedalState::SLOT_EQ,
          "order 2 is Drive then EQ");

    Chain c1;
    c1.Init(kSr);
    auto ra = Run(c1, a, 24000, 4800, [](size_t n) { return 0.4f * Sine(n, 80.f); });

    Chain c2;
    c2.Init(kSr);
    auto rb = Run(c2, b, 24000, 4800, [](size_t n) { return 0.4f * Sine(n, 80.f); });

    char detail[160];
    snprintf(detail, sizeof detail, "(EQ->Drive rms %.4f vs Drive->EQ rms %.4f)", ra.rms, rb.rms);
    Check(ra.finite && rb.finite && std::fabs(ra.rms - rb.rms) > 0.01f,
          "swapping EQ and Drive in the chain changes the sound",
          detail);
}

static void TestAllSixOrdersVisitEveryEffect()
{
    PedalState s;
    s.Reset();

    bool ok = true;
    for(int order = 0; order < PedalState::kOrderCount; order++)
    {
        bool seen[PedalState::SLOT_LAST] = {false, false, false};
        for(int pos = 0; pos < PedalState::SLOT_LAST; pos++)
            seen[s.SlotAt(pos)] = true;
        for(int slot = 0; slot < PedalState::SLOT_LAST; slot++)
            if(!seen[slot])
                ok = false;
        s.NextOrder();
    }
    Check(ok, "every one of the six orders contains each effect exactly once");

    // Prev and Next must be inverses, including across the wrap.
    PedalState t;
    t.Reset();
    t.PrevOrder();
    const int wrapped = t.GetOrder();
    t.NextOrder();
    Check(wrapped == PedalState::kOrderCount - 1 && t.GetOrder() == 0,
          "chain order wraps in both directions");
}

static void TestSlotAmountBypassesSlot()
{
    // Reverb at zero amount must be inaudible, whatever its own mix says.
    PedalState s;
    Neutral(s);
    s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_MIX, 1.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_REVERB_AMOUNT, 0.0f);

    Chain chain;
    chain.Init(kSr);
    auto r = Run(chain, s, 24000, 4800, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    char detail[128];
    snprintf(detail, sizeof detail, "(peak %.4f, expected 0.5)", r.peak);
    Check(r.finite && std::fabs(r.peak - 0.5f) < 0.01f,
          "a slot at zero amount is fully bypassed",
          detail);
}

static void TestSlotBypassIsTransparentAndRestores()
{
    // A bypassed slot must be silent regardless of its amount knob, and
    // un-bypassing must bring back exactly what the knob still says.
    PedalState on;
    Neutral(on);
    on.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 1.0f);
    on.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_GAIN, 0.9f);
    on.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_MIX, 1.0f);

    PedalState off = on;
    off.SetSlotBypass(PedalState::SLOT_DRIVE, true);

    Chain c1;
    c1.Init(kSr);
    auto bypassed
        = Run(c1, off, 24000, 4800, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    Chain c2;
    c2.Init(kSr);
    auto active
        = Run(c2, on, 24000, 4800, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    char detail[160];
    snprintf(detail, sizeof detail, "(bypassed peak %.4f = dry 0.5, active %.4f)",
             bypassed.peak, active.peak);
    Check(std::fabs(bypassed.peak - 0.5f) < 0.01f && active.peak > 0.6f,
          "bypassing a slot passes it dry; the amount knob is untouched",
          detail);

    // The amount knob must survive the round trip, so switching back in
    // restores the setting rather than resetting it.
    PedalState back = off;
    back.SetSlotBypass(PedalState::SLOT_DRIVE, false);
    Check(back.SlotAmount(PedalState::SLOT_DRIVE) == 1.0f
              && back.EffectiveSlotAmount(PedalState::SLOT_DRIVE) == 1.0f,
          "un-bypassing restores the amount that was dialled in");
}

static void TestMetaKnobOrder()
{
    // The meta page interleaves amounts with gains, so SlotAmount() is a lookup
    // and the easiest thing in the build to get silently wrong.
    PedalState s;
    s.Reset();
    s.SetKnob(PedalState::PAGE_META, 0, 0.11f); // KNOB_1 eq amount
    s.SetKnob(PedalState::PAGE_META, 1, 0.22f); // KNOB_2 reverb amount
    s.SetKnob(PedalState::PAGE_META, 2, 0.33f); // KNOB_3 in gain
    s.SetKnob(PedalState::PAGE_META, 3, 0.44f); // KNOB_4 drive amount
    s.SetKnob(PedalState::PAGE_META, 4, 0.55f); // KNOB_5 out level
    s.SetKnob(PedalState::PAGE_META, 5, 0.66f); // KNOB_6 global mix

    char detail[160];
    snprintf(detail, sizeof detail, "(eq %.2f drive %.2f reverb %.2f, in %.2f out %.2f mix %.2f)",
             s.SlotAmount(PedalState::SLOT_EQ),
             s.SlotAmount(PedalState::SLOT_DRIVE),
             s.SlotAmount(PedalState::SLOT_REVERB),
             s.Meta(PedalState::META_IN_GAIN),
             s.Meta(PedalState::META_OUT_LEVEL),
             s.Meta(PedalState::META_MIX));

    Check(s.SlotAmount(PedalState::SLOT_EQ) == 0.11f
              && s.SlotAmount(PedalState::SLOT_REVERB) == 0.22f
              && s.Meta(PedalState::META_IN_GAIN) == 0.33f
              && s.SlotAmount(PedalState::SLOT_DRIVE) == 0.44f
              && s.Meta(PedalState::META_OUT_LEVEL) == 0.55f
              && s.Meta(PedalState::META_MIX) == 0.66f,
          "meta knobs 1-6 map to eq/reverb/in/drive/out/mix",
          detail);
}

static void TestSavedStateRoundTrip()
{
    // Everything that should survive a power cycle must come back identical.
    PedalState before;
    before.Reset();
    for(int p = 0; p < PedalState::PAGE_LAST; p++)
        for(int k = 0; k < PedalState::kKnobCount; k++)
            before.SetKnob((PedalState::Page)p, k, (p * 6 + k) / 24.0f);
    before.SetPage(PedalState::PAGE_REVERB);
    before.SetOrder(4);
    before.SetSlotBypass(PedalState::SLOT_DRIVE, true);
    before.SetBypass(false);

    const SavedState saved = CaptureState(before);

    PedalState after;
    after.Reset();
    const bool ok = ApplyState(saved, &after);

    bool same = ok;
    for(int p = 0; p < PedalState::PAGE_LAST && same; p++)
        for(int k = 0; k < PedalState::kKnobCount && same; k++)
            same = after.GetKnob((PedalState::Page)p, k)
                   == before.GetKnob((PedalState::Page)p, k);
    same = same && after.GetPage() == PedalState::PAGE_REVERB
           && after.GetOrder() == 4
           && after.SlotBypassed(PedalState::SLOT_DRIVE)
           && !after.SlotBypassed(PedalState::SLOT_EQ)
           && !after.IsBypassed();

    Check(same, "saved settings survive a capture/apply round trip");
    Check(CaptureState(after) == saved, "and re-capture to the identical block");
}

static void TestSavedStateRejectsGarbage()
{
    // Flash that has never been written reads as whatever was left in it, so
    // ApplyState has to refuse anything it does not recognise rather than boot
    // the pedal with nonsense in its parameters.
    PedalState reference;
    reference.Reset();
    const SavedState good = CaptureState(reference);

    SavedState bad = good;
    bad.version = 999;
    PedalState s1;
    s1.Reset();
    Check(!ApplyState(bad, &s1), "a block with an unknown version is refused");

    bad = good;
    bad.page = 77;
    Check(!ApplyState(bad, &s1), "an out-of-range page is refused");

    bad = good;
    bad.order = -3;
    Check(!ApplyState(bad, &s1), "an out-of-range chain order is refused");

    bad = good;
    bad.param[0][0] = 4.2f;
    Check(!ApplyState(bad, &s1), "a parameter outside 0..1 is refused");

    bad = good;
    bad.param[1][2] = std::nan("");
    Check(!ApplyState(bad, &s1), "a NaN parameter is refused");

    // A refusal must leave the model untouched, not half-written.
    Check(CaptureState(s1) == good, "a refused block leaves the model alone");
}

static void TestSavedStateDetectsChange()
{
    // PersistentStorage only erases when the block differs, so != has to be
    // right or settings either never persist or rewrite flash constantly.
    PedalState s;
    s.Reset();
    const SavedState a = CaptureState(s);
    PedalState reference;
    reference.Reset();

    Check(!(a != CaptureState(s)), "an unchanged model compares equal");

    s.SetKnob(PedalState::PAGE_DRIVE, 3, 0.77f);
    Check(a != CaptureState(s), "a moved knob compares different");

    PedalState t;
    t.Reset();
    t.ToggleSlotBypass(PedalState::SLOT_REVERB);
    Check(a != CaptureState(t), "a slot bypass compares different");

    // The one that matters on hardware: a picked-up knob is rewritten from its
    // pot every block and the smoothed ADC value wanders in the last decimals.
    // Compared exactly, an untouched pedal looks like it is being changed a
    // thousand times a second, the save never settles, and nothing is ever
    // written. That is precisely the bug this catches.
    PedalState jitter;
    jitter.Reset();
    for(int k = 0; k < PedalState::kKnobCount; k++)
        jitter.SetKnob(PedalState::PAGE_EQ, k,
                       reference.GetKnob(PedalState::PAGE_EQ, k)
                           + (k % 2 ? 1 : -1) * 0.0004f);
    Check(!(a != CaptureState(jitter)),
          "ADC jitter below the epsilon does NOT compare as a change");

    PedalState nudged;
    nudged.Reset();
    nudged.SetKnob(PedalState::PAGE_EQ, 0,
                   reference.GetKnob(PedalState::PAGE_EQ, 0) + 0.01f);
    Check(a != CaptureState(nudged),
          "a real one-percent move still compares as a change");
}

static void TestMidiMapDecodes()
{
    using namespace midimap;

    bool ok = true;
    // Every page's six knobs, in order.
    const struct { uint8_t base; int page; } blocks[] = {
        {kEqBase, PedalState::PAGE_EQ},
        {kDriveBase, PedalState::PAGE_DRIVE},
        {kReverbBase, PedalState::PAGE_REVERB},
        {kMetaBase, PedalState::PAGE_META},
    };
    for(const auto& b : blocks)
        for(int k = 0; k < PedalState::kKnobCount; k++)
        {
            Binding d = Decode(b.base + k);
            if(d.target != Target::PARAM || d.page != b.page || d.knob != k)
                ok = false;
        }
    Check(ok, "all 24 page parameters decode to the right page and knob");

    Check(Decode(kMasterBypass).target == Target::MASTER_BYPASS,
          "master bypass decodes");
    ok = true;
    for(int s = 0; s < PedalState::SLOT_LAST; s++)
    {
        Binding d = Decode(kSlotBase + s);
        if(d.target != Target::SLOT_BYPASS || d.slot != s)
            ok = false;
    }
    Check(ok, "the three slot bypasses decode to the right slots");
    Check(Decode(kChainOrder).target == Target::CHAIN_ORDER, "chain order decodes");
    Check(Decode(kPageSelect).target == Target::PAGE_SELECT, "page select decodes");
}

static void TestMidiMapIgnoresEverythingElse()
{
    using namespace midimap;

    // A pedal that lurched on every modulation or volume message would be
    // unusable, so anything unmapped must decode to NONE.
    int mapped = 0;
    bool reserved_clear = true;
    for(int cc = 0; cc < 128; cc++)
    {
        if(Decode((uint8_t)cc).target != Target::NONE)
        {
            mapped++;
            // 32-63 are the LSBs of controllers 0-31; a 14-bit controller
            // would move two of our parameters at once.
            if(cc == 1 || cc == 7 || cc == 11 || (cc >= 32 && cc <= 63)
               || (cc >= 64 && cc <= 69) || cc >= 120 || cc == 0)
                reserved_clear = false;
        }
    }
    char detail[64];
    snprintf(detail, sizeof detail, "(%d of 128 controllers mapped)", mapped);
    Check(mapped == 30, "exactly 30 controllers are claimed", detail);
    Check(reserved_clear,
          "none of them land on defined or LSB controller numbers");
}

static void TestMidiScaling()
{
    using namespace midimap;

    Check(Normalise(0) == 0.0f && Normalise(127) == 1.0f,
          "a controller at full travel reaches exactly 1.0");

    // Quantise must cover every slot and never run off the end.
    bool ok = true, saw_last = false, saw_first = false;
    for(int v = 0; v < 128; v++)
    {
        int q = Quantise((uint8_t)v, PedalState::kOrderCount);
        if(q < 0 || q >= PedalState::kOrderCount)
            ok = false;
        if(q == 0)
            saw_first = true;
        if(q == PedalState::kOrderCount - 1)
            saw_last = true;
    }
    Check(ok && saw_first && saw_last,
          "chain order quantises across all six without overrunning");
    Check(Quantise(127, PedalState::PAGE_LAST) == PedalState::PAGE_LAST - 1,
          "a controller at full travel selects the last page");

    Check(!IsOn(63) && IsOn(64), "switch controllers turn on at half travel");
}

static void TestPresetBankRoundTrip()
{
    SavedBank bank{};
    bank.version = SavedBank::kVersion;
    bank.current = 3;

    PedalState s;
    for(int i = 0; i < SavedBank::kPresetCount; i++)
    {
        s.Reset();
        s.SetKnob(PedalState::PAGE_EQ, 0, i / 32.0f);
        s.SetOrder(i % PedalState::kOrderCount);
        bank.preset[i] = CaptureState(s);
    }

    SavedBank copy = bank;
    Check(!(copy != bank), "an unchanged bank compares equal");

    copy.preset[7].param[0][0] += 0.5f;
    Check(copy != bank, "a change in any one preset makes the bank differ");

    copy = bank;
    copy.current = 9;
    Check(copy != bank, "changing the current preset makes the bank differ");

    // Each slot must come back as itself, not as its neighbour.
    PedalState out;
    out.Reset();
    bool ok = ApplyState(bank.preset[5], &out);
    ok = ok && out.GetOrder() == (5 % PedalState::kOrderCount);
    Check(ok, "a preset applies back as the settings it was captured from");
}

static void TestPageSlotMapping()
{
    Check(PedalState::PageSlot(PedalState::PAGE_EQ) == PedalState::SLOT_EQ
              && PedalState::PageSlot(PedalState::PAGE_DRIVE) == PedalState::SLOT_DRIVE
              && PedalState::PageSlot(PedalState::PAGE_REVERB) == PedalState::SLOT_REVERB,
          "each effect page maps to its own slot for the short-press bypass");
    Check(PedalState::PageSlot(PedalState::PAGE_META) == PedalState::SLOT_LAST,
          "the meta page maps to no slot, so a short press there does nothing");
}

static void TestGlobalMixAndLevels()
{
    PedalState s;
    Neutral(s);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_MIX, 0.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_OUT_LEVEL, 0.0f);

    Chain chain;
    chain.Init(kSr);
    auto r = Run(chain, s, 24000, 4800, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    char detail[128];
    snprintf(detail, sizeof detail, "(peak %.4f, expected 0.5 dry)", r.peak);
    Check(std::fabs(r.peak - 0.5f) < 0.01f,
          "global mix fully dry passes the input untouched even at zero output level",
          detail);

    // Output level at centre is unity, at the top is +12 dB.
    PedalState hot;
    Neutral(hot);
    hot.SetKnob(PedalState::PAGE_META, PedalState::META_OUT_LEVEL, 1.0f);
    Chain c2;
    c2.Init(kSr);
    auto rh = Run(c2, hot, 24000, 4800, [](size_t n) { return 0.1f * Sine(n, 440.f); });

    snprintf(detail, sizeof detail, "(%.1f dB)", Db(rh.peak / 0.1f));
    Check(std::fabs(Db(rh.peak / 0.1f) - 12.f) < 0.5f,
          "output level reaches +12 dB at full travel",
          detail);
}

static void TestBypassIsClean()
{
    PedalState s;
    Neutral(s);
    s.SetBypass(true);
    // Everything cranked; bypass must still pass through untouched.
    s.SetKnob(PedalState::PAGE_EQ, PedalState::EQ_LOW_GAIN, 1.0f);
    s.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 1.0f);
    s.SetKnob(PedalState::PAGE_DRIVE, PedalState::DRIVE_GAIN, 1.0f);

    Chain chain;
    chain.Init(kSr);
    auto r = Run(chain, s, 4800, 2400, [](size_t n) { return 0.5f * Sine(n, 440.f); });

    char detail[128];
    snprintf(detail, sizeof detail, "(peak %.4f, expected 0.5)", r.peak);
    Check(std::fabs(r.peak - 0.5f) < 1e-4f, "bypass passes the input bit for bit", detail);
}

static void TestOddBlockSizes()
{
    // Chain chunks internally at kMaxBlockSize; a block larger than that, and a
    // block that is not a multiple of it, must both come out the same as small
    // blocks would.
    PedalState s;
    Neutral(s);

    const size_t n = 1000;
    std::vector<float> in_l(n), in_r(n), out_a_l(n), out_a_r(n), out_b_l(n), out_b_r(n);
    for(size_t i = 0; i < n; i++)
    {
        in_l[i] = 0.4f * Sine(i, 300.f);
        in_r[i] = in_l[i];
    }

    Chain a;
    a.Init(kSr);
    for(size_t off = 0; off < n; off += 48)
    {
        const size_t sz     = (n - off) < 48 ? (n - off) : 48;
        const float* ip[2]  = {in_l.data() + off, in_r.data() + off};
        float*       op[2]  = {out_a_l.data() + off, out_a_r.data() + off};
        a.Process(s, ip, op, sz);
    }

    Chain b;
    b.Init(kSr);
    const float* ip[2] = {in_l.data(), in_r.data()};
    float*       op[2] = {out_b_l.data(), out_b_r.data()};
    b.Process(s, ip, op, n); // one 1000-frame block, far over kMaxBlockSize

    float worst = 0.f;
    for(size_t i = 0; i < n; i++)
        worst = std::max(worst, std::fabs(out_a_l[i] - out_b_l[i]));

    char detail[128];
    snprintf(detail, sizeof detail, "(largest sample difference %.2e)", worst);
    Check(worst < 1e-5f, "a 1000-frame block matches the same audio in 48-frame blocks", detail);
}

int main()
{
    printf("\nBoonta MultiEffect - host DSP tests\n\n");

    TestUnity();
    TestEqBoostAndCut();
    TestDriveBounded();
    TestDriveBiasDcBlocked();
    TestReverbDecaysAndIsStable();
    TestReverbSizesStable();
    TestChainOrderMatters();
    TestAllSixOrdersVisitEveryEffect();
    TestSlotAmountBypassesSlot();
    TestSlotBypassIsTransparentAndRestores();
    TestMetaKnobOrder();
    TestPageSlotMapping();
    TestSavedStateRoundTrip();
    TestSavedStateRejectsGarbage();
    TestSavedStateDetectsChange();
    TestMidiMapDecodes();
    TestMidiMapIgnoresEverythingElse();
    TestMidiScaling();
    TestPresetBankRoundTrip();
    TestGlobalMixAndLevels();
    TestBypassIsClean();
    TestOddBlockSizes();

    printf("\n%s (%d failing)\n\n", failures ? "FAILURES" : "all passed", failures);
    return failures ? 1 : 0;
}
