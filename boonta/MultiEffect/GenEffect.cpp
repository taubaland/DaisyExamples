#include "GenEffect.h"

#ifdef BOONTA_GEN_EFFECT

#include <string.h>

namespace gen = BOONTA_GEN_EFFECT;

/** The vector size handed to create(). Chain never gives an effect more than
 *  this many frames at a time, so a patch that sizes internal buffers from it
 *  is safe. */
static constexpr int kBlockHint = 128;

/** Describe the patch to the rest of the pedal. Edit this to match. */
const EffectDesc& GenEffect::Desc() const
{
    static const EffectDesc kDesc = {
        "Gen",
        0.2f, 1.0f, 0.4f, // a colour of its own, distinct from the built-ins
        "unused by this patch",
        {{"param 1"}, {"param 2"}, {"param 3"},
         {"param 4"}, {"param 5"}, {"param 6"}},
    };
    return kDesc;
}

/** Knob to patch parameter. The identity is only right if the patch declares
 *  exactly six parameters in the order you want them on the page; otherwise
 *  name the indices you mean. */
int GenEffect::ParamIndex(int knob)
{
    return knob;
}

/** 0-1 from a pot, to whatever the patch wants. */
float GenEffect::Scale(int knob, float value)
{
    (void)knob;
    return value;
}

void GenEffect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    // gen~ allocates its state at create() time. That is a one-off at boot,
    // never on the audio thread, and never again -- which is the only shape of
    // allocation this firmware permits.
    if(!state_)
        state_ = gen::create(sample_rate, kBlockHint);

    gen::reset(static_cast<gen::State*>(state_));

    // Force every parameter through on the first block: the patch comes up with
    // whatever it was exported with, not with what the knobs say.
    primed_ = false;
}

void GenEffect::Process(const float* params,
                        int          toggle,
                        float*       left,
                        float*       right,
                        size_t       size)
{
    (void)toggle; // this patch has no use for the slot's toggle

    if(!state_)
        return;

    gen::State* self = static_cast<gen::State*>(state_);

    // Push only what moved. setparameter is cheap but not free, and a patch
    // that recomputes coefficients on every set would pay for it six times a
    // block for nothing.
    for(int i = 0; i < Effect::kParamCount; i++)
    {
        if(primed_ && params[i] == last_[i])
            continue;
        last_[i] = params[i];
        gen::setparameter(self, ParamIndex(i), Scale(i, params[i]), nullptr);
    }
    primed_ = true;

    // gen~ works in non-interleaved buffers, which is what the chain already
    // hands round, so this is a pointer pair rather than a copy.
    t_sample* ins[2]  = {left, right};
    t_sample* outs[2] = {left, right};

    gen::perform(self, ins, 2, outs, 2, static_cast<long>(size));
}

#endif // BOONTA_GEN_EFFECT
