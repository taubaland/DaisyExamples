#include "Controls.h"

#include <math.h>

using namespace daisy;

/** Toggle i belongs to slot i, and what it selects is whatever the effect in
 *  that slot says it selects. No table: the mapping is the identity, which is
 *  the only arrangement that survives the effects being interchangeable. */

/** How close the pot has to get before a parked knob counts as picked up.
 *  Slack here trades a small jump for an easier catch; one percent of travel is
 *  below what you can hear on any of these parameters and well above the noise
 *  left by the AnalogControl smoothing. */
static constexpr float kPickupWindow = 0.01f;

/** How far a knob has to move from where it stood on arriving at a page before
 *  that page counts as edited. Comfortably above the ADC noise left after
 *  smoothing, and well below a deliberate turn. */
static constexpr float kEditWindow = 0.01f;

/** How long the page footswitch has to be held before it counts as a long
 *  press. Shorter than the 500 ms DaisyBoonta::CheckButtonLongPress() uses,
 *  because that felt sluggish under a foot: this switch is held deliberately to
 *  change page, not to guard against a mis-tap. Going much below this starts
 *  turning firm short presses into page changes. */
static constexpr float kLongPressMs = 300.f;

/** Which page and knob the expression pedal takes over.
 *
 *  Off by default, and deliberately so: GetExpression() reads 0 with nothing
 *  plugged in, so pointing this at (PAGE_META, META_MIX) -- the obvious
 *  choice -- would silently pin the pedal fully dry for anyone without an
 *  expression pedal. Set both to enable. */
static constexpr PedalState::Page kExpressionPage = PedalState::PAGE_LAST;
static constexpr int              kExpressionKnob = 0;

void Controls::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_    = hw;
    state_ = state;

    // PAGE_LAST is not a page, so the first Process() sees the page as having
    // changed and parks the knobs. That has to happen there rather than here:
    // StartAdc() has not run yet, so every pot would read zero and park against
    // the wrong side of its stored value.
    //
    // Parking at boot -- rather than adopting the pots -- is what makes saved
    // settings mean anything. The pedal comes up sounding as you left it, and a
    // knob takes over when you sweep it through its stored value.
    last_page_ = PedalState::PAGE_LAST;
}

void Controls::Process()
{
    hw_->ProcessAllControls();

    // Buttons first: a page change or a reorder should take effect on the same
    // block that produced it, not one block late.
    ReadButtons();
    ReadKnobs();
    ReadToggles();
}

void Controls::RePark()
{
    ParkKnobs(state_->GetPage());
}

void Controls::ParkKnobs(PedalState::Page page)
{
    for(int i = 0; i < PedalState::kKnobCount; i++)
    {
        const float pot
            = hw_->GetKnobValue(static_cast<DaisyBoonta::Knob>(i));
        const float stored = state_->GetKnob(page, i);

        armed_[i]         = false;
        entered_above_[i] = (pot - stored) > 0.f;
        entry_value_[i]   = stored;
    }

    // Nothing on this page has been changed yet, so the page LED pulses.
    state_->SetPageEdited(false);
}

void Controls::ReadKnobs()
{
    const PedalState::Page page = state_->GetPage();

    if(page != last_page_)
    {
        ParkKnobs(page);
        last_page_ = page;
    }

    uint32_t pending = 0;

    for(int i = 0; i < PedalState::kKnobCount; i++)
    {
        const float pot
            = hw_->GetKnobValue(static_cast<DaisyBoonta::Knob>(i));

        if(!armed_[i])
        {
            const float diff = pot - state_->GetKnob(page, i);

            // Picked up either by landing on the value or by crossing it. The
            // crossing test is what makes a fast sweep work: at block rate a
            // quick turn can step clean over the window without ever landing
            // inside it.
            const bool crossed = (diff > 0.f) != entered_above_[i];

            // A pot that stops short of its rail can never reach a stored 1.0,
            // and a knob that stays parked no matter how far you turn it reads
            // as a dead pedal. Treat the end of travel as having got there.
            const bool unreachable = (pot >= 1.f - kPickupWindow && diff < 0.f)
                                     || (pot <= kPickupWindow && diff > 0.f);

            if(fabsf(diff) <= kPickupWindow || crossed || unreachable)
                armed_[i] = true;
            else
                pending |= (1u << i);
        }

        if(armed_[i])
        {
            state_->SetKnob(page, i, pot);

            // The page LED reports whether anything here has actually been
            // *changed*, so compare against the value on arrival rather than
            // against the previous block -- an armed knob is rewritten from its
            // pot every block, so a frame-to-frame comparison reads as zero
            // however far you turn it. Picking up is not enough either: a pot
            // already sitting on its stored value arms on the first block
            // without anything being edited.
            if(fabsf(pot - entry_value_[i]) > kEditWindow)
                state_->SetPageEdited(true);
        }
    }

    state_->SetPickupPending(pending);

    const float exp = hw_->GetExpression();
    state_->SetExpression(exp);

    if(kExpressionPage != PedalState::PAGE_LAST)
        state_->SetKnob(kExpressionPage, kExpressionKnob, exp);
}

void Controls::ReadToggles()
{
    for(size_t i = 0; i < DaisyBoonta::TOG_SW_LAST; i++)
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

        state_->SetToggle(static_cast<int>(i), mapped);
    }
}

void Controls::ReadButtons()
{
    // Left footswitch does two jobs. A short press switches the current page's
    // effect in or out; holding it moves to the next page.
    //
    // The long press fires the moment the threshold passes rather than waiting
    // for your foot to come up, because a page change you can only feel on
    // release is a page change you cannot time. Having fired, it latches, so
    // the release does not also read as a short press. DaisyBoonta has a
    // CheckButtonLongPress(), but it stays true for as long as you hold, which
    // would advance a page per block.
    Switch& page_sw = hw_->switches[DaisyBoonta::SW_FS_1];

    if(page_sw.RisingEdge())
        fs1_long_fired_ = false;

    if(!fs1_long_fired_ && page_sw.TimeHeldMs() > kLongPressMs)
    {
        state_->NextPage();
        fs1_long_fired_ = true;
    }

    if(page_sw.FallingEdge() && !fs1_long_fired_)
    {
        // The meta page edits the chain rather than any one effect, so there is
        // nothing for a short press to switch out there.
        const PedalState::Slot slot
            = PedalState::PageSlot(state_->GetPage());

        if(slot != PedalState::SLOT_LAST)
            state_->ToggleSlotBypass(slot);
    }

    // Right footswitch: latching master bypass, driving the relays.
    if(hw_->switches[DaisyBoonta::SW_FS_2].RisingEdge())
        state_->ToggleBypass();

    // Select buttons: step the chain order in either direction. Six orders, so
    // the worst case from any order to any other is three presses.
    if(hw_->switches[DaisyBoonta::SW_SEL_1].RisingEdge())
        state_->PrevOrder();

    if(hw_->switches[DaisyBoonta::SW_SEL_2].RisingEdge())
        state_->NextOrder();
}
