#ifdef BOONTA_RNBO

#define BOONTA_RNBO_PATCH_NAME patch_freezer
#define BOONTA_RNBO_PATCH_FILE rnbo.freezer.cpp
#include "RnboPatch.h"

#include "RnboEffect.h"
#include "RnboEffects.h"

/** A freezer. Twelve parameters against six knobs and a toggle, so this mapping
 *  is mostly a decision about what to leave alone.
 *
 *  **`freeze` is on the toggle, and that is the weak point of this mapping.**
 *  It is the gesture the effect exists for and it is binary, so it wants a
 *  footswitch -- but an Effect is handed six knobs and one toggle and nothing
 *  else, and all four of the pedal's switches are already spoken for (two
 *  select, one page, one bypass). The toggle is the only switch an effect can
 *  reach, so it takes it, and the centre position goes to waste. Giving freeze
 *  a footswitch means widening the Effect interface; that is a real change and
 *  has not been made on a guess.
 *
 *  Transpose gets the whole +/-24 for the same reason -- the toggle that
 *  narrows it on the pitch shifter is busy here.
 *
 *  Left wherever the patch was exported: auto, autodel, panjit, ampjit, direct.
 *  Nobody has played this, so which six deserve knobs is provisional.
 */
namespace
{
const EffectDesc kDesc = {
    "Freeze",
    0.0f,
    0.3f,
    1.0f, // blue
    "freeze: off / off / on",
    {{"transpose"},
     {"fade"},
     {"low cut"},
     {"high cut"},
     {"volume"},
     {"pitch jitter"}},
};

const RnboParam kMap[Effect::kParamCount] = {
    {0, -24.f, 24.f},   // transp
    {7, 0.f, 100.f},    // fade
    {4, 0.f, 100.f},    // lowcut
    {5, 0.f, 100.f},    // highcut
    {6, -100.f, 100.f}, // volume
    {8, 0.f, 100.f},    // pitchjit
};

const RnboPatchSpec kSpec = {
    &kDesc,
    kMap,
    {3, {0.f, 0.f, 1.f}}, // freeze: only the top position freezes
    {kRnboUnmapped, {0.f, 0.f, 0.f}},
};

RnboEffect<BoontaPatcher> effect(kSpec);
} // namespace

Effect* RnboFreezer()
{
    return &effect;
}

#endif // BOONTA_RNBO
