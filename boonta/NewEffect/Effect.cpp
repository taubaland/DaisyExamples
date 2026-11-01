#include "Effect.h"

// DaisySP is linked and on the include path; its oscillators, filters and
// helpers (fonepole, SoftClip, TWOPI_F ...) are what most effects are built
// out of. Drop the include if the effect ends up not needing it.
#include "daisysp.h"

using namespace daisysp;

void Effect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    // Clear per-channel state here.
}

void Effect::Process(const PedalState&   state,
                     const float* const* in,
                     float**             out,
                     size_t              size)
{
    // Hard bypass: the relays have already routed the signal around the Daisy,
    // so nothing here is audible. Copy through and skip the work rather than
    // burning cycles on samples nobody will hear.
    if(state.IsBypassed())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
        }
        return;
    }

    // --- control rate --------------------------------------------------
    // Resolve the model into coefficients once per block, not once per sample.
    // Every state.GetParam() is 0..1; map it to whatever range the effect
    // wants here, and read the toggles with state.GetToggle(). The expression
    // pedal, the preset index, the alt footswitch and the tapped tempo are all
    // on `state` too -- see PedalState.h.

    // --- audio rate ----------------------------------------------------
    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = ProcessChannel(0, in[0][i]);
        out[1][i] = ProcessChannel(1, in[1][i]);
    }
}

float Effect::ProcessChannel(int channel, float in)
{
    // Pass through unchanged until there is an effect to write.
    (void)channel;

    return in;
}
