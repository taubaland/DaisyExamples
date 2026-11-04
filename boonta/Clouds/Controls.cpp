#include "Controls.h"

using namespace daisy;

/** Physical knob -> parameter meaning. This table is the whole knob mapping:
 *  change it here and the effect, the LEDs and the docs all follow. */
static constexpr PedalState::Param kKnobToParam[DaisyBoonta::KNOB_LAST] = {
    PedalState::PARAM_POSITION, // KNOB_1
    PedalState::PARAM_SIZE,     // KNOB_2
    PedalState::PARAM_PITCH,    // KNOB_3
    PedalState::PARAM_DENSITY,  // KNOB_4
    PedalState::PARAM_TEXTURE,  // KNOB_5
    PedalState::PARAM_MIX,      // KNOB_6
};

/** Physical toggle -> toggle meaning. */
static constexpr PedalState::Toggle kToggleMap[3] = {
    PedalState::TOGGLE_SPREAD,   // TOG_SW_1
    PedalState::TOGGLE_FEEDBACK, // TOG_SW_2
    PedalState::TOGGLE_REVERB,   // TOG_SW_3
};

/** Which parameter the expression pedal takes over when it is moved.
 *
 *  Position is the one worth a foot: sweeping it walks the playback point
 *  through the buffer. Nothing detects whether a pedal is plugged in -- the
 *  jack is an ADC channel and nothing more -- so expression has to earn its
 *  parameter by moving, the same way a parked pot does. Set to PARAM_LAST to
 *  ignore expression entirely. */
static constexpr PedalState::Param kExpressionTarget
    = PedalState::PARAM_POSITION;

/** How long the freeze switch has to be down before it latches.
 *
 *  Long enough that a stutter tap never trips it, short enough that latching
 *  does not feel like waiting. Switch::TimeHeldMs() measures from the rising
 *  edge, so this is wall time from first contact. */
static constexpr float kFreezeHoldMs = 700.f;

/** How far a parked control has to move before it takes its parameter back.
 *
 *  Two percent of travel. Comfortably above the ADC noise that survives
 *  DaisyBoonta's smoothing, and small enough that a deliberate nudge is
 *  enough -- you should not have to sweep a knob to reclaim it. Once a
 *  control is live it tracks continuously; this threshold only gates the
 *  handover. */
static constexpr float kTakeoverDelta = 0.02f;

void Controls::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_                = hw;
    state_             = state;
    freeze_latched_    = false;
    freeze_hold_fired_ = false;
    toggles_seeded_    = false;

    for(size_t i = 0; i < DaisyBoonta::KNOB_LAST; i++)
        knob_pickup_[i] = Pickup{};

    exp_pickup_ = Pickup{};

    for(size_t i = 0; i < 3; i++)
        toggle_seen_[i] = PedalState::POS_MID;
}

void Controls::Process()
{
    hw_->ProcessAllControls();

    ReadKnobs();
    ReadToggles();
    ReadFootswitches();
    ReadSelects();
}

bool Controls::Claim(Pickup& pu, float value, float current, bool claim_at_boot)
{
    if(!pu.seeded)
    {
        pu.seeded = true;
        pu.anchor = value;
        pu.live   = claim_at_boot;

        if(!pu.live)
            return false;

        pu.wrote = value;
        return true;
    }

    // The model is not holding what this control last put there, so somebody
    // else wrote the parameter. Stand down, and remember where the control
    // was standing: movement is measured from here, not from the parameter.
    if(pu.live && current != pu.wrote)
    {
        pu.live   = false;
        pu.anchor = value;
    }

    if(!pu.live)
    {
        const float moved = value - pu.anchor;
        if((moved < 0.f ? -moved : moved) < kTakeoverDelta)
            return false;

        pu.live = true;
    }

    pu.wrote = value;
    return true;
}

void Controls::ReadKnobs()
{
    for(size_t i = 0; i < DaisyBoonta::KNOB_LAST; i++)
    {
        const float value
            = hw_->GetKnobValue(static_cast<DaisyBoonta::Knob>(i));
        const PedalState::Param p = kKnobToParam[i];

        if(Claim(knob_pickup_[i], value, state_->GetParam(p), true))
            state_->SetParam(p, value);
    }

    const float exp = hw_->GetExpression();
    state_->SetExpression(exp);

    if(kExpressionTarget != PedalState::PARAM_LAST)
    {
        if(Claim(exp_pickup_, exp, state_->GetParam(kExpressionTarget), false))
            state_->SetParam(kExpressionTarget, exp);
    }
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

        // Write only when the switch itself has moved, so a position sent
        // over MIDI stands until somebody flips it back.
        if(!toggles_seeded_ || mapped != toggle_seen_[i])
        {
            toggle_seen_[i] = mapped;
            state_->SetToggle(kToggleMap[i], mapped);
        }
    }

    toggles_seeded_ = true;
}

void Controls::ReadFootswitches()
{
    // --- left footswitch: freeze, momentary or latched ------------------
    //
    // One switch, two gestures, and the point is that you never have to
    // decide which one you are making until after you have made it:
    //
    //   press         freeze engages immediately, for as long as you hold it
    //   release early it lets go -- a tap is a stutter, a hold is a pad
    //   keep holding  past kFreezeHoldMs the latch flips, and freeze stays
    //                 engaged when you let go
    //   hold again    past the threshold the latch flips back, and freeze
    //                 drops the moment it does rather than waiting for the
    //                 release, so the sound follows the gesture
    //
    // While latched, a short tap momentarily *releases* freeze: the buffer
    // starts recording again for as long as the switch is down. That falls
    // out of the exclusive-or below and is worth knowing -- it is how you
    // punch a new phrase into a held drone.
    //
    // MIDI freeze is a separate flag in the model, ORed with this one, so a
    // controller cannot disable the switch and the switch cannot cancel a
    // controller.
    Switch& freeze_sw = hw_->switches[DaisyBoonta::SW_FS_1];

    const bool held = freeze_sw.Pressed();

    if(freeze_sw.RisingEdge())
        freeze_hold_fired_ = false;

    if(held && !freeze_hold_fired_ && freeze_sw.TimeHeldMs() >= kFreezeHoldMs)
    {
        freeze_latched_    = !freeze_latched_;
        freeze_hold_fired_ = true;
    }

    // Once the hold has fired, the latch alone decides: holding the switch
    // down after it has done its job must not invert the result.
    const bool frozen
        = freeze_hold_fired_ ? freeze_latched_ : (freeze_latched_ != held);

    state_->SetFreeze(frozen);
    state_->SetFreezeLatched(freeze_latched_);

    // --- right footswitch: latching bypass ------------------------------
    //
    // Edge driven, so it needs no pickup rule: it writes the model when it is
    // pressed and never otherwise, which leaves a MIDI bypass standing.
    if(hw_->switches[DaisyBoonta::SW_FS_2].RisingEdge())
        state_->ToggleBypass();
}

void Controls::ReadSelects()
{
    if(hw_->switches[DaisyBoonta::SW_SEL_1].RisingEdge())
        state_->PrevMode();

    if(hw_->switches[DaisyBoonta::SW_SEL_2].RisingEdge())
        state_->NextMode();
}
