#include "PedalState.h"

void PedalState::Reset()
{
    for(int i = 0; i < PARAM_LAST; i++)
        param_[i] = 0.f;
    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = POS_MID;

    expression_      = 0.f;
    bypassed_        = true; // boot up out of circuit
    alt_held_        = false;
    preset_          = 0;
    tap_interval_ms_ = 500.f;
}

void PedalState::SetPreset(int preset)
{
    // Wrap in both directions so the select buttons can cycle either way.
    preset_ = ((preset % kPresetCount) + kPresetCount) % kPresetCount;
}
