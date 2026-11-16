#ifdef BOONTA_RNBO

#define BOONTA_RNBO_PATCH_NAME patch_pitchshifter
#define BOONTA_RNBO_PATCH_FILE rnbo.pitchshifter.cpp
#include "RnboPatch.h"

#include "RnboEffect.h"
#include "RnboEffects.h"

/** A pitch shifter. Four parameters, so two knobs are spare and are left that
 *  way. The toggle chooses how far the transpose knob reaches: a pitch shifter
 *  spends most of its life within a few semitones, and a knob that has to cover
 *  four octaves cannot be set to a fifth by hand. Centre detent is unison at
 *  every setting.
 */
namespace
{
const EffectDesc kDesc = {
    "Pitch",
    1.0f,
    0.45f,
    0.0f, // orange: distinct from teal, gold and purple
    "transpose range: +/-5 / +/-12 / +/-24 semitones",
    {{"transpose"}, {"mix"}, {"spread"}, {"window"}, {"--"}, {"--"}},
};

const RnboParam kMap[Effect::kParamCount] = {
    {0, -24.f, 24.f},   // transp, range narrowed by the toggle below
    {1, 0.f, 100.f},    // mix, per cent
    {2, -100.f, 100.f}, // spread
    {3, 2.f, 200.f},    // window
    {kRnboUnmapped, 0.f, 0.f},
    {kRnboUnmapped, 0.f, 0.f},
};

const RnboPatchSpec kSpec = {
    &kDesc,
    kMap,
    {kRnboUnmapped, {0.f, 0.f, 0.f}},
    {0, {5.f, 12.f, 24.f}},
};

RnboEffect<BoontaPatcher> effect(kSpec);
} // namespace

Effect* RnboPitchshifter()
{
    return &effect;
}

#endif // BOONTA_RNBO
