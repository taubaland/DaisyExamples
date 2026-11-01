#include "Controls.h"

using namespace daisy;

/** Physical knob -> parameter meaning. This table is the whole knob mapping:
 *  change it here and the effect, the LEDs and the docs all follow. */
static constexpr PedalState::Param kKnobToParam[DaisyBoonta::KNOB_LAST] = {
    PedalState::PARAM_DRIVE,    // KNOB_1
    PedalState::PARAM_TONE,     // KNOB_2
    PedalState::PARAM_LEVEL,    // KNOB_3
    PedalState::PARAM_MIX,      // KNOB_4
    PedalState::PARAM_TIME,     // KNOB_5
    PedalState::PARAM_FEEDBACK, // KNOB_6
};

/** Physical toggle -> toggle meaning. */
static constexpr PedalState::Toggle kToggleMap[3] = {
    PedalState::TOGGLE_RANGE,     // TOG_SW_1
    PedalState::TOGGLE_CHARACTER, // TOG_SW_2
    PedalState::TOGGLE_ROUTING,   // TOG_SW_3
};

/** Which parameter the expression pedal takes over when it is plugged in.
 *  Set to PARAM_LAST to ignore expression entirely. */
static constexpr PedalState::Param kExpressionTarget = PedalState::PARAM_LAST;

/** Taps further apart than this start a new tempo instead of extending one. */
static constexpr uint32_t kTapTimeoutMs = 2000;

void Controls::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_          = hw;
    state_       = state;
    last_tap_ms_ = 0;
}

void Controls::Process()
{
    hw_->ProcessAllControls();

    ReadKnobs();
    ReadToggles();
    ReadFootswitches();
    ReadSelects();
}

void Controls::ReadKnobs()
{
    for(size_t i = 0; i < DaisyBoonta::KNOB_LAST; i++)
    {
        const float value
            = hw_->GetKnobValue(static_cast<DaisyBoonta::Knob>(i));
        state_->SetParam(kKnobToParam[i], value);
    }

    const float exp = hw_->GetExpression();
    state_->SetExpression(exp);

    if(kExpressionTarget != PedalState::PARAM_LAST)
        state_->SetParam(kExpressionTarget, exp);
}

void Controls::ReadToggles()
{
    for(size_t i = 0; i < 3; i++)
    {
        const int pos
            = hw_->GetSwitchPosition(static_cast<DaisyBoonta::TogSw>(i));

        PedalState::TogglePos mapped;
        switch(pos)
        {
            case Switch3::POS_UP: mapped = PedalState::POS_HIGH; break;
            case Switch3::POS_DOWN: mapped = PedalState::POS_LOW; break;
            default: mapped = PedalState::POS_MID; break;
        }

        state_->SetToggle(kToggleMap[i], mapped);
    }
}

void Controls::ReadFootswitches()
{
    // Left footswitch: latching bypass.
    if(hw_->switches[DaisyBoonta::SW_FS_1].RisingEdge())
        state_->ToggleBypass();

    // Right footswitch: momentary "alt" for the effect to use however it
    // likes, plus tap tempo on each press.
    state_->SetAltHeld(hw_->switches[DaisyBoonta::SW_FS_2].Pressed());

    if(hw_->switches[DaisyBoonta::SW_FS_2].RisingEdge())
    {
        const uint32_t now      = System::GetNow();
        const uint32_t interval = now - last_tap_ms_;

        if(last_tap_ms_ != 0 && interval < kTapTimeoutMs)
            state_->SetTapIntervalMs(static_cast<float>(interval));

        last_tap_ms_ = now;
    }
}

void Controls::ReadSelects()
{
    if(hw_->switches[DaisyBoonta::SW_SEL_1].RisingEdge())
        state_->PrevPreset();

    if(hw_->switches[DaisyBoonta::SW_SEL_2].RisingEdge())
        state_->NextPreset();
}
