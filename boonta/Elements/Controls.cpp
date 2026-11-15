#include "Controls.h"

using namespace daisy;

/** Physical toggle -> toggle meaning. */
static constexpr PedalState::Toggle kToggleMap[3] = {
    PedalState::TOGGLE_MODEL,   // TOG_SW_1
    PedalState::TOGGLE_TRIGGER, // TOG_SW_2
    PedalState::TOGGLE_ROUTE,   // TOG_SW_3
};

/** Which parameter the expression pedal takes over when it is moved.
 *
 *  Geometry is the one worth a foot: it morphs the struck object from plate
 *  to bar to string, or walks the chord table in the strings model. Nothing
 *  detects whether a pedal is plugged in -- the jack is an ADC channel and
 *  nothing more -- so expression has to earn its parameter by moving, the
 *  same way a parked pot does. Set to PARAM_LAST to ignore expression. */
static constexpr PedalState::Param kExpressionTarget
    = PedalState::PARAM_GEOMETRY;

/** How long the gate switch has to be down before it latches.
 *
 *  Long enough that playing a note never trips it, short enough that
 *  latching does not feel like waiting. Switch::TimeHeldMs() measures from
 *  the rising edge, so this is wall time from first contact. */
static constexpr float kGateHoldMs = 700.f;

/** How close a parked control has to come to its parameter to take it back.
 *  Ten percent of travel; see Clouds' Controls.cpp for the reasoning. */
static constexpr float kCatchWindow = 0.10f;

/** How far a parked control must also have moved since it was parked. Two
 *  percent of travel, above the ADC noise that survives the smoothing. */
static constexpr float kTakeoverDelta = 0.02f;

static_assert(PedalState::kKnobsPerPage
                  == static_cast<int>(DaisyBoonta::KNOB_LAST),
              "a page is one of each knob");

void Controls::Init(DaisyBoonta* hw, PedalState* state, bool restored)
{
    restored_        = restored;
    hw_              = hw;
    state_           = state;
    gate_latched_    = false;
    gate_hold_fired_ = false;
    toggles_seeded_  = false;
    first_block_     = true;
    page_seen_       = state->GetPage();

    for(int i = 0; i < PedalState::PARAM_LAST; i++)
        param_pickup_[i] = Pickup{};

    exp_pickup_ = Pickup{};

    for(size_t i = 0; i < 3; i++)
        toggle_seen_[i] = PedalState::POS_MID;
}

void Controls::Process()
{
    hw_->ProcessAllControls();

    // Selects first, so a page change and the parking it causes happen in
    // the same block and no knob writes through to a parameter it has not
    // caught yet.
    ReadSelects();
    ReadKnobs();
    ReadToggles();
    ReadFootswitches();

    first_block_ = false;
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
        const float off   = value - current;

        // Has to have been turned, and has to have arrived near the value.
        if((moved < 0.f ? -moved : moved) < kTakeoverDelta
           || (off < 0.f ? -off : off) > kCatchWindow)
            return false;

        pu.live = true;
    }

    pu.wrote = value;
    return true;
}

void Controls::ReadKnobs()
{
    const int  page   = state_->GetPage();
    const bool turned = page != page_seen_;
    page_seen_        = page;

    // Only the page that is up when the pedal boots may take its parameters
    // from the panel outright, and only if nothing was restored. Every other
    // parameter keeps its default, or its saved value, until its knob is
    // turned to it.
    const bool claim = first_block_ && !restored_;

    for(int i = 0; i < PedalState::kKnobsPerPage; i++)
    {
        const float value
            = hw_->GetKnobValue(static_cast<DaisyBoonta::Knob>(i));
        const PedalState::Param p = static_cast<PedalState::Param>(
            page * PedalState::kKnobsPerPage + i);
        Pickup& pu = param_pickup_[p];

        // The page has just turned (by a button or by MIDI), so this pot is
        // pointing at the value of a parameter it no longer controls. Park
        // it where it stands.
        if(turned)
        {
            pu.seeded = true;
            pu.live   = false;
            pu.anchor = value;
        }

        if(Claim(pu, value, state_->GetParam(p), claim))
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
        // over MIDI stands until somebody flips it back. The first read
        // records where the switch is, and writes it only if nothing was
        // restored from saved state.
        if(!toggles_seeded_)
        {
            toggle_seen_[i] = mapped;
            if(!restored_)
                state_->SetToggle(kToggleMap[i], mapped);
        }
        else if(mapped != toggle_seen_[i])
        {
            toggle_seen_[i] = mapped;
            state_->SetToggle(kToggleMap[i], mapped);
        }
    }

    toggles_seeded_ = true;
}

void Controls::ReadFootswitches()
{
    // --- left footswitch: the gate, momentary or latched ----------------
    //
    // The same two-gesture switch as Clouds' freeze, playing a note instead:
    //
    //   press         the gate goes up at once: a note, for as long as you
    //                 hold it
    //   release early the gate drops -- a tap is a pluck, a hold is a bow
    //   keep holding  past kGateHoldMs the latch flips, and the gate stays
    //                 up when you let go: a drone you can walk away from
    //   hold again    past the threshold the latch flips back
    //
    // While latched, a short tap momentarily *drops* the gate and raises it
    // again on release, which re-strikes the note. That falls out of the
    // exclusive-or below.
    Switch& gate_sw = hw_->switches[DaisyBoonta::SW_FS_1];

    const bool held = gate_sw.Pressed();

    if(gate_sw.RisingEdge())
        gate_hold_fired_ = false;

    if(held && !gate_hold_fired_ && gate_sw.TimeHeldMs() >= kGateHoldMs)
    {
        gate_latched_    = !gate_latched_;
        gate_hold_fired_ = true;
    }

    // Once the hold has fired, the latch alone decides: holding the switch
    // down after it has done its job must not invert the result.
    const bool gate
        = gate_hold_fired_ ? gate_latched_ : (gate_latched_ != held);

    state_->SetGate(gate);
    state_->SetGateLatched(gate_latched_);

    // --- right footswitch: latching bypass ------------------------------
    if(hw_->switches[DaisyBoonta::SW_FS_2].RisingEdge())
        state_->ToggleBypass();
}

void Controls::ReadSelects()
{
    if(hw_->switches[DaisyBoonta::SW_SEL_1].RisingEdge())
        state_->PrevPage();

    if(hw_->switches[DaisyBoonta::SW_SEL_2].RisingEdge())
        state_->NextPage();
}
