#include "PedalState.h"

void PedalState::Reset()
{
    for(int i = 0; i < PARAM_LAST; i++)
        param_[i] = 0.f;
    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = POS_MID;

    expression_     = 0.f;
    bypassed_       = true; // boot up out of circuit
    frozen_         = false;
    freeze_latched_ = false;
    freeze_remote_  = false;
    quantise_       = QUANT_FREE;
    output_level_   = 0.f;
    input_peak_     = 0.f;
}

void PedalState::SetQuantise(int q)
{
    // Wrap in both directions so the select buttons can cycle either way.
    quantise_ = ((q % kQuantiseCount) + kQuantiseCount) % kQuantiseCount;
}
