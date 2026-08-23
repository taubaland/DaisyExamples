#pragma once
#ifndef BOONTA_CONTROLS_H
#define BOONTA_CONTROLS_H

#include "daisy_boonta.h"
#include "PedalState.h"

/** Controller.
 *
 *  The only place that touches input hardware. It reads knobs, toggles,
 *  footswitches and expression, turns them into intent (page advanced, chain
 *  reordered, drive set to 0.6) and writes that intent into the model. It never
 *  drives an LED and it never processes a sample.
 *
 *  Its one piece of real logic is soft pickup. Six pots have to serve four
 *  pages, so on arrival at a page a pot almost never sits where that page left
 *  its parameter. Rather than let the value snap to the pot -- which would make
 *  every page change an audible lurch, and would mean the pedal did not really
 *  retain anything -- each knob is *parked* until the pot sweeps through the
 *  stored value, and only then starts tracking. The model publishes which knobs
 *  are still parked so the view can say so.
 *
 *  Call Process() exactly once per audio callback: DaisyBoonta initialises its
 *  AnalogControl smoothing filters with AudioCallbackRate(), so calling at any
 *  other rate detunes the knob smoothing.
 */
class Controls
{
  public:
    Controls()
    : hw_(nullptr),
      state_(nullptr),
      last_page_(PedalState::PAGE_EQ),
      fs1_long_fired_(true)
    {
    }

    void Init(daisy::DaisyBoonta* hw, PedalState* state);

    /** Read every input and update the model. Audio-callback context. */
    void Process();

  private:
    void ReadKnobs();
    void ReadToggles();
    void ReadButtons();

    /** Park every knob against the given page, recording which side of the
     *  stored value each pot currently sits on. */
    void ParkKnobs(PedalState::Page page);

    /** Take every knob live immediately, for the page showing at boot. */
    void ArmKnobs();

    daisy::DaisyBoonta* hw_;
    PedalState*         state_;

    PedalState::Page last_page_;
    bool             armed_[PedalState::kKnobCount];
    /** Pot was above the stored value when the page was entered. Pickup
     *  happens when that stops being true. */
    bool             entered_above_[PedalState::kKnobCount];

    /** The current hold of the page footswitch has already advanced a page, so
     *  the release must not also toggle a bypass. Starts true so a release seen
     *  before any press -- at boot, with a foot already down -- does nothing. */
    bool             fs1_long_fired_;
};

#endif
