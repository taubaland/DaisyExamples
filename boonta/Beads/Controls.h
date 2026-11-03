#pragma once
#ifndef BOONTA_CONTROLS_H
#define BOONTA_CONTROLS_H

#include "daisy_boonta.h"
#include "PedalState.h"

/** Controller.
 *
 *  The only place that touches input hardware. It reads knobs, toggles,
 *  footswitches and expression, turns them into intent (freeze engaged,
 *  quantisation changed, time set to 0.6) and writes that intent into the model. It
 *  never drives an LED and it never processes a sample.
 *
 *  Call Process() exactly once per audio callback: DaisyBoonta initialises its
 *  AnalogControl smoothing filters with AudioCallbackRate(), so calling at any
 *  other rate detunes the knob smoothing.
 *
 *  ## Sharing the model with a MIDI controller
 *
 *  A pot writes its parameter every block, so without something to stop it a
 *  value sent over MIDI would last exactly one block. The rule here is that a
 *  control gives way the moment anything else writes its parameter, and takes
 *  it back the moment it is actually moved.
 *
 *  Noticing the first half needs no plumbing between the two controllers at
 *  all: each control remembers the value it last wrote, and if the model no
 *  longer holds that value then somebody else wrote it. That generalises for
 *  free -- MIDI today, preset recall tomorrow -- and costs one float compare
 *  per control. Taking it back has to mean real movement rather than mere
 *  disagreement, because a parked pot is almost always pointing somewhere
 *  other than where its parameter now is.
 */
class Controls
{
  public:
    Controls()
    : hw_(nullptr), state_(nullptr), freeze_latched_(false),
      freeze_hold_fired_(false), toggles_seeded_(false)
    {
    }

    void Init(daisy::DaisyBoonta* hw, PedalState* state);

    /** Read every input and update the model. Audio-callback context. */
    void Process();

  private:
    /** One continuous control's claim on its parameter. */
    struct Pickup
    {
        float wrote  = 0.f; /**< the value this control last wrote */
        float anchor = 0.f; /**< where it stood when it was parked  */
        bool  live   = false;
        bool  seeded = false;
    };

    void ReadKnobs();
    void ReadToggles();
    void ReadFootswitches();
    void ReadSelects();

    /** Should this control write its parameter this block?
     *
     *  \param pu            the control's pickup state
     *  \param value         what the control reads now
     *  \param current       what the model holds for the parameter
     *  \param claim_at_boot true for the panel pots, which own the model
     *                       until something takes it from them; false for
     *                       expression, which has to be moved before it
     *                       counts -- an unplugged jack is still an ADC
     *                       channel reading *something*, and that something
     *                       would otherwise pin its parameter forever.
     */
    bool Claim(Pickup& pu, float value, float current, bool claim_at_boot);

    daisy::DaisyBoonta* hw_;
    PedalState*         state_;

    /** Freeze footswitch state machine. `freeze_latched_` survives the
     *  release; `freeze_hold_fired_` records that the current press has
     *  already crossed the long-hold threshold, so it only toggles the latch
     *  once however long the switch is held down. */
    bool freeze_latched_;
    bool freeze_hold_fired_;

    Pickup knob_pickup_[daisy::DaisyBoonta::KNOB_LAST];
    Pickup exp_pickup_;

    /** Toggles are discrete, so "has it moved" is just "is it where it was"
     *  and no anchor or threshold is needed. */
    PedalState::TogglePos toggle_seen_[3];
    bool                  toggles_seeded_;
};

#endif
