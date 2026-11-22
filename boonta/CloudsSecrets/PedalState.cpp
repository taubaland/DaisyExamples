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
    mode_           = MODE_GRANULAR;
    output_level_   = 0.f;
}

void PedalState::SetMode(int mode)
{
    mode_ = ((mode % MODE_LAST) + MODE_LAST) % MODE_LAST;
}

void PedalState::StepAlt(int dir)
{
    if(mode_ == MODE_GRANULAR)
        return; // the hidden layer is closed; the buttons have nothing to pick

    // Cycle over the hidden modes only, in either direction.
    const int i = mode_ - 1;
    mode_       = 1 + ((i + dir + kAltModeCount) % kAltModeCount);
}

void PedalState::ToggleAlt()
{
    mode_ = (mode_ == MODE_GRANULAR) ? MODE_STRETCH : MODE_GRANULAR;
}

PedalState::Saved PedalState::Capture() const
{
    Saved s;
    s.magic = kSavedMagic;

    for(int i = 0; i < PARAM_LAST; i++)
    {
        // Clamped here as well as checked on the way back in, so a value
        // that has strayed can never make Capture() differ from itself.
        const float v = param_[i];
        s.param[i]    = (v >= 0.f && v <= 1.f) ? v : (v > 1.f ? 1.f : 0.f);
    }
    for(int i = 0; i < TOGGLE_LAST; i++)
        s.toggle[i] = static_cast<uint8_t>(toggle_[i]);

    s.mode = static_cast<uint8_t>(mode_);
    return s;
}

bool PedalState::Restore(const Saved& saved)
{
    if(saved.magic != kSavedMagic)
        return false;

    for(int i = 0; i < PARAM_LAST; i++)
    {
        const float v = saved.param[i];
        if(!(v >= 0.f && v <= 1.f)) // false for NaN as well
            return false;
    }
    for(int i = 0; i < TOGGLE_LAST; i++)
        if(saved.toggle[i] > POS_HIGH)
            return false;
    if(saved.mode >= kModeCount)
        return false;

    for(int i = 0; i < PARAM_LAST; i++)
        param_[i] = saved.param[i];
    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = static_cast<TogglePos>(saved.toggle[i]);
    SetMode(saved.mode);
    return true;
}
