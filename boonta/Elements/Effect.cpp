#include "Effect.h"

#include <math.h>

#include "elements/dsp/part.h"
#include "stmlib/dsp/dsp.h"

using elements::Patch;
using elements::PerformanceState;

// --- voicing --------------------------------------------------------------

/** Lowest note of the pitch knob, and how many semitones it spans. C1 to C5:
 *  four octaves, so each semitone is about 2% of travel -- findable by hand,
 *  which five octaves on a pedal-sized pot is not. */
static constexpr float kPitchBase  = 24.f;
static constexpr float kPitchRange = 48.f;

/** Fine tune, semitones either side of centre. */
static constexpr float kFineRange = 1.f;

/** Pitch bend, semitones either way. Matches midimap::kBendRange. */
static constexpr float kBendRange = 2.f;

/** Input gain at the top of the INPUT knob. The knob is squared, so half
 *  travel is unity. */
static constexpr float kInputGainMax = 4.f;

/** Gain into the output limiter. Upstream's: its output stage is
 *  SoftLimit(x * 0.5), then a hard clip at the converter. Both halves are
 *  needed -- the Part limits itself *before* its reverb, SoftLimit is only
 *  soft up to about three times full scale, and a resonant patch driven hard
 *  gets past both. */
static constexpr float kOutputGain = 0.5f;

/** Per-chunk smoothing coefficient. Chunks run 2000 times a second, so this
 *  is a 25 ms time constant: upstream's pot filter, near enough. */
static constexpr float kSmooth = 0.02f;

// --- the input as a gate ----------------------------------------------------

/** Below this the input is treated as silence, as upstream does. */
static constexpr float kNoiseGateThreshold = 0.0001f;

/** Gate thresholds on the input's peak, with hysteresis between them.
 *  About -34 dBFS to open and -42 to close. */
static constexpr float kGateOn  = 0.02f;
static constexpr float kGateOff = 0.008f;

/** Peak follower decay per chunk: a 30 ms release. */
static constexpr float kEnvDecay = 0.9835f;

/** How fast the floor rises towards the fast follower: about 50 ms. It
 *  falls with it at once. */
static constexpr float kFloorRise = 0.01f;

/** A new attack is the fast follower standing this far above the floor
 *  while the gate is already open: a 6 dB jump. */
static constexpr float kAttackRatio = 2.f;

/** Chunks (half a millisecond each) before another attack can retrigger,
 *  and how long the gate is held low to make the retrigger an edge. */
static constexpr int kHoldoffChunks   = 120; // 60 ms
static constexpr int kRetriggerChunks = 4;   // 2 ms

static inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

void Effect::Init(elements::Part* part,
                  uint16_t*       reverb_buffer,
                  uint32_t*       seed,
                  size_t          seed_len)
{
    part_ = part;
    part_->Init(reverb_buffer);
    if(seed != nullptr && seed_len > 0)
        part_->Seed(seed, seed_len);

    // Part::Init() leaves panic_ and easter_egg_ to .bss. So does this, by
    // requiring zeroed storage, but saying so costs nothing.
    part_->set_easter_egg(false);
    part_->set_bypass(false);

    PedalState defaults;
    for(int i = 0; i < PedalState::PARAM_LAST; i++)
        smoothed_[i] = defaults.GetParam(static_cast<PedalState::Param>(i));

    pitch_semitones_ = static_cast<int>(
        smoothed_[PedalState::PARAM_PITCH] * kPitchRange + 0.5f);

    input_level_ = 0.f;
    env_         = 0.f;
    env_floor_   = 0.f;
    input_gate_  = false;
    holdoff_     = 0;
    retrigger_   = 0;

    tracker_.Init();
    tracker_generation_ = tracker_.Generation();
}

void Effect::UpdatePatch(const PedalState& state)
{
    for(int i = 0; i < PedalState::PARAM_LAST; i++)
    {
        const float target
            = state.GetParam(static_cast<PedalState::Param>(i));
        smoothed_[i] += kSmooth * (target - smoothed_[i]);
    }

    // The ceiling is upstream's: several of these index a table, and 1.0
    // exactly is one past the end of it.
    auto p = [this](PedalState::Param which) {
        return Clamp(smoothed_[which], 0.f, 0.9995f);
    };

    Patch* patch = part_->mutable_patch();

    patch->exciter_envelope_shape = p(PedalState::PARAM_CONTOUR);
    patch->exciter_bow_level      = p(PedalState::PARAM_BOW);
    patch->exciter_blow_level     = p(PedalState::PARAM_BLOW);
    patch->exciter_strike_level   = p(PedalState::PARAM_STRIKE);

    patch->exciter_bow_timbre    = p(PedalState::PARAM_BOW_TIMBRE);
    patch->exciter_blow_meta     = p(PedalState::PARAM_FLOW);
    patch->exciter_blow_timbre   = p(PedalState::PARAM_BLOW_TIMBRE);
    patch->exciter_strike_meta   = p(PedalState::PARAM_MALLET);
    patch->exciter_strike_timbre = Clamp(
        smoothed_[PedalState::PARAM_STRIKE_TIMBRE], 0.f, 0.995f);

    patch->resonator_geometry   = p(PedalState::PARAM_GEOMETRY);
    patch->resonator_brightness = p(PedalState::PARAM_BRIGHTNESS);
    patch->resonator_damping    = p(PedalState::PARAM_DAMPING);
    patch->resonator_position   = p(PedalState::PARAM_POSITION);

    // The panel pot's range. Upstream lets CV push this to 2.0, where the
    // reverb freezes; there is no CV here to do it.
    patch->space = Clamp(smoothed_[PedalState::PARAM_SPACE], 0.f, 1.f);

    switch(state.GetToggle(PedalState::TOGGLE_MODEL))
    {
        case PedalState::MODEL_MODAL:
            part_->set_resonator_model(elements::RESONATOR_MODEL_MODAL);
            break;
        case PedalState::MODEL_STRING:
            part_->set_resonator_model(elements::RESONATOR_MODEL_STRING);
            break;
        default:
            part_->set_resonator_model(elements::RESONATOR_MODEL_STRINGS);
            break;
    }
}

bool Effect::InputGate(float chunk_peak)
{
    // Fast follower: instant attack, 30 ms release.
    env_ *= kEnvDecay;
    if(chunk_peak > env_)
        env_ = chunk_peak;

    if(holdoff_ > 0)
        holdoff_--;

    if(!input_gate_)
    {
        if(env_ > kGateOn)
        {
            input_gate_ = true;
            holdoff_    = kHoldoffChunks;
        }
    }
    else if(env_ < kGateOff)
    {
        input_gate_ = false;
    }
    else if(holdoff_ == 0 && env_ > kAttackRatio * env_floor_)
    {
        // The gate is already open and the level has jumped: a new note
        // played over the tail of the last one. Drop the gate for a moment
        // so the exciters see an edge.
        retrigger_ = kRetriggerChunks;
        holdoff_   = kHoldoffChunks;
    }

    // The floor is the level the note in progress has decayed to. It has to
    // follow the level down immediately: a follower that merely lags sits
    // *above* a decaying note, and the next attack then never clears it.
    if(env_ < env_floor_)
        env_floor_ = env_;
    else
        env_floor_ += kFloorRise * (env_ - env_floor_);

    if(retrigger_ > 0)
    {
        retrigger_--;
        return false;
    }
    return input_gate_;
}

float Effect::ProcessChunk(const PedalState& state,
                           const float*      in_l,
                           const float*      in_r,
                           float*            out_l,
                           float*            out_r,
                           size_t            size)
{
    UpdatePatch(state);

    // --- the input jack ----------------------------------------------------
    const float knob = smoothed_[PedalState::PARAM_INPUT];
    const float gain = knob * knob * kInputGainMax;

    const PedalState::TogglePos route
        = state.GetToggle(PedalState::TOGGLE_ROUTE);
    const float to_strike = route == PedalState::ROUTE_BLOW ? 0.f : 1.f;
    const float to_blow   = route == PedalState::ROUTE_STRIKE ? 0.f : 1.f;

    float blow_in[kChunk];
    float strike_in[kChunk];
    float mono[kChunk];
    float peak_in = 0.f;

    for(size_t i = 0; i < size; i++)
    {
        // The pedal is mono in; summing costs nothing if only one side is
        // wired and is right if both are.
        mono[i] = 0.5f * (in_l[i] + in_r[i]);
        float x = mono[i] * gain;

        // The trigger and the tracker read the jack as it arrives, not as
        // the Input knob has scaled it: that knob says how hard the guitar
        // drives the resonator, and has nothing to say about whether a note
        // was played. Input at zero must still follow the guitar.
        const float a = mono[i] < 0.f ? -mono[i] : mono[i];
        if(a > peak_in)
            peak_in = a;

        // Upstream's noise gate, verbatim in behaviour: a fast-attack
        // slow-release power detector, fading the input out below the
        // threshold. Without it the codec's idle noise rings the resonator.
        const float error = x * x - input_level_;
        input_level_ += error * (error > 0.f ? 0.1f : 0.0001f);
        if(input_level_ <= kNoiseGateThreshold)
            x *= input_level_ * (1.f / kNoiseGateThreshold);

        strike_in[i] = x * to_strike;
        blow_in[i]   = x * to_blow;
    }

    // --- pitch -------------------------------------------------------------
    // From the raw input, ahead of the Input knob: turning the guitar's level
    // into the resonator down must not blind the tracker.
    tracker_.Process(mono, size);
    const bool follow = state.GetToggle(PedalState::TOGGLE_TRIGGER)
                        == PedalState::TRIGGER_INPUT;

    // --- performance -------------------------------------------------------
    // Always run, so the follower's state is current the moment the toggle
    // selects it.
    const bool from_input = InputGate(peak_in);

    // A new note from the tracker arrives some 30 ms after the attack that
    // caused it, by which time the exciters have already struck the old
    // pitch. Re-strike on it, so the note that rings is the one that was
    // played.
    if(tracker_.Generation() != tracker_generation_)
    {
        tracker_generation_ = tracker_.Generation();
        if(follow && input_gate_)
            retrigger_ = kRetriggerChunks;
    }

    bool gate = state.Gate();
    switch(state.GetToggle(PedalState::TOGGLE_TRIGGER))
    {
        case PedalState::TRIGGER_INPUT: gate = gate || from_input; break;
        case PedalState::TRIGGER_DRONE: gate = true; break;
        default: break;
    }

    // Coarse pitch, quantised with upstream's hysteresis.
    const float n = smoothed_[PedalState::PARAM_PITCH] * kPitchRange;
    const float hysteresis
        = n - static_cast<float>(pitch_semitones_) > 0.f ? -0.3f : 0.3f;
    pitch_semitones_ = static_cast<int>(n + hysteresis + 0.5f);

    PerformanceState perf;
    perf.gate = gate;
    // Following the guitar, the Pitch knob stops being an absolute note and
    // becomes a transposition: centre is unison, and its four octaves of
    // travel run two down to two up.
    const float base
        = follow ? static_cast<float>(tracker_.Note())
                       + static_cast<float>(pitch_semitones_)
                       - 0.5f * kPitchRange
                 : kPitchBase + static_cast<float>(pitch_semitones_);
    perf.note = base
                + (smoothed_[PedalState::PARAM_FINE] - 0.5f) * 2.f * kFineRange
                + state.NoteOffset() + state.Bend() * kBendRange;
    perf.modulation = 0.f;
    perf.strength   = Clamp(
        smoothed_[PedalState::PARAM_STRENGTH] * state.Velocity(), 0.f, 1.f);

    // --- render ------------------------------------------------------------
    float main[kChunk];
    float aux[kChunk];
    part_->Process(perf, blow_in, strike_in, main, aux, size);

    float peak = 0.f;
    for(size_t i = 0; i < size; i++)
    {
        // main is centre minus side and aux is centre plus side.
        const float l
            = Clamp(stmlib::SoftLimit(aux[i] * kOutputGain), -1.f, 1.f);
        const float r
            = Clamp(stmlib::SoftLimit(main[i] * kOutputGain), -1.f, 1.f);
        out_l[i]      = l;
        out_r[i]      = r;

        const float al = l < 0.f ? -l : l;
        const float ar = r < 0.f ? -r : r;
        if(al > peak)
            peak = al;
        if(ar > peak)
            peak = ar;
    }

    return peak;
}

float Effect::Process(PedalState&         state,
                      const float* const* in,
                      float**             out,
                      size_t              size)
{
    // Out of circuit the relays have routed the jacks around the codec, so
    // nothing rendered here could be heard. Spend nothing on it.
    if(!state.IsActive())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = 0.f;
            out[1][i] = 0.f;
        }
        input_gate_ = false;
        env_        = 0.f;
        env_floor_  = 0.f;
        state.SetSounding(false);
        return 0.f;
    }

    float  peak = 0.f;
    size_t done = 0;
    while(done < size)
    {
        const size_t n = size - done > kChunk ? kChunk : size - done;
        const float  p = ProcessChunk(state,
                                      in[0] + done,
                                      in[1] + done,
                                      out[0] + done,
                                      out[1] + done,
                                      n);
        if(p > peak)
            peak = p;
        done += n;
    }

    // What the gate footswitch's LED shows: a note is being played, by
    // whatever is playing it.
    state.SetSounding(part_->gate());
    return peak;
}
