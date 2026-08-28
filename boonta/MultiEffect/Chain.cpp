#include "Chain.h"

#include <math.h>

/** Input trim: unity at the centre of the pot, +/-12 dB at the ends. A trim,
 *  not a level, so it never mutes -- turning the chain down belongs on the
 *  output knob where you would look for it. */
static inline float InputTrim(float knob)
{
    return powf(4.f, (knob - 0.5f) * 2.f);
}

/** Output level: silence at the bottom, unity at the centre, +12 dB at the top. */
static inline float OutputLevel(float knob)
{
    return 4.f * knob * knob;
}

void Chain::Init(float sample_rate)
{
    // Every registered effect, not only the three currently in slots: swapping
    // one in must not be the moment it first gets initialised.
    effects::InitAll(sample_rate);
}

void Chain::Process(const PedalState&   state,
                    const float* const* in,
                    float**             out,
                    size_t              size)
{
    // Hard bypass: the relays route around us, so just pass through and let the
    // filter state settle rather than burning cycles.
    if(state.IsBypassed())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
        }
        return;
    }

    for(size_t offset = 0; offset < size; offset += kMaxBlockSize)
    {
        const size_t remaining = size - offset;
        const size_t chunk = remaining < kMaxBlockSize ? remaining : kMaxBlockSize;

        ProcessChunk(state,
                     in[0] + offset,
                     in[1] + offset,
                     out[0] + offset,
                     out[1] + offset,
                     chunk);
    }
}

void Chain::ProcessChunk(const PedalState& state,
                         const float*      in_l,
                         const float*      in_r,
                         float*            out_l,
                         float*            out_r,
                         size_t            size)
{
    const float trim = InputTrim(state.Meta(PedalState::META_IN_GAIN));

    for(size_t i = 0; i < size; i++)
    {
        dry_l_[i]  = in_l[i];
        dry_r_[i]  = in_r[i];
        work_l_[i] = in_l[i] * trim;
        work_r_[i] = in_r[i] * trim;
    }

    for(int position = 0; position < PedalState::SLOT_LAST; position++)
    {
        const PedalState::Slot slot = state.SlotAt(position);

        // Bypass forces the slot fully dry without touching its amount knob, so
        // switching it back in restores whatever was dialled in.
        const float amount = state.EffectiveSlotAmount(slot);

        // Keep this slot's input for the crossfade below.
        for(size_t i = 0; i < size; i++)
        {
            slot_l_[i] = work_l_[i];
            slot_r_[i] = work_r_[i];
        }

        // Every slot runs even when bypassed or at zero amount. Skipping would
        // save cycles, but it would also freeze the reverb tank mid-tail, so
        // switching the reverb back in would resume a stale tail rather than a
        // decayed one -- and switching an effect out would cut its tail dead
        // instead of letting it ring out into the dry signal.
        RunSlot(slot, state, work_l_, work_r_, size);

        for(size_t i = 0; i < size; i++)
        {
            work_l_[i] = slot_l_[i] + amount * (work_l_[i] - slot_l_[i]);
            work_r_[i] = slot_r_[i] + amount * (work_r_[i] - slot_r_[i]);
        }
    }

    const float level = OutputLevel(state.Meta(PedalState::META_OUT_LEVEL));
    const float mix   = state.Meta(PedalState::META_MIX);

    for(size_t i = 0; i < size; i++)
    {
        const float wet_l = work_l_[i] * level;
        const float wet_r = work_r_[i] * level;

        out_l[i] = dry_l_[i] + mix * (wet_l - dry_l_[i]);
        out_r[i] = dry_r_[i] + mix * (wet_r - dry_r_[i]);
    }
}

void Chain::RunSlot(PedalState::Slot  slot,
                    const PedalState& state,
                    float*            left,
                    float*            right,
                    size_t            size)
{
    if(slot >= PedalState::SLOT_LAST)
        return;

    // The slot says which effect; the effect is handed its six parameters and
    // its own toggle, and told nothing else. Slot i uses toggle i.
    Effect* effect = effects::Get(state.GetSlotEffect(slot));

    effect->Process(state.SlotParams(slot),
                    static_cast<int>(state.GetToggle(slot)),
                    left,
                    right,
                    size);
}
