#ifdef BOONTA_RNBO

#define BOONTA_RNBO_PATCH_NAME patch_octaver
#define BOONTA_RNBO_PATCH_FILE rnbo.octaver.cpp
#include "RnboPatch.h"

#include "RnboEffect.h"
#include "RnboEffects.h"

/** An octaver. Seven parameters against six knobs and a toggle, so one has to
 *  go somewhere: `rectified` is the one that behaves like a mode rather than a
 *  control, so it takes the toggle and the other six take knobs. The two octave
 *  voices lead, because they are what the effect is for.
 *
 *  Provisional -- nobody has played it yet.
 */
namespace
{
const EffectDesc kDesc = {
    "Octave",
    1.0f,
    0.0f,
    0.0f, // red
    "rectified: off / half / full",
    {{"octave 1"}, {"octave 2"}, {"mix"}, {"drive"}, {"tone"}, {"AM"}},
};

const RnboParam kMap[Effect::kParamCount] = {
    {6, 0.f, 100.f}, // octave1
    {5, 0.f, 100.f}, // octave2
    {1, 0.f, 100.f}, // mix
    {2, 0.f, 100.f}, // drive
    {3, 0.f, 100.f}, // tone
    {0, 0.f, 100.f}, // AM
};

const RnboPatchSpec kSpec = {
    &kDesc,
    kMap,
    {4, {0.f, 50.f, 100.f}}, // rectified, on the toggle
    {kRnboUnmapped, {0.f, 0.f, 0.f}},
};

RnboEffect<BoontaPatcher> effect(kSpec);
} // namespace

Effect* RnboOctaver()
{
    return &effect;
}

#endif // BOONTA_RNBO
