#pragma once
#ifndef BOONTA_CONTROLS_H
#define BOONTA_CONTROLS_H

#include "daisy_boonta.h"
#include "PedalState.h"

/** Controller.
 *
 *  The only place that touches input hardware. It reads knobs, toggles,
 *  footswitches and expression, turns them into intent (bypass toggled, preset
 *  changed, drive set to 0.6) and writes that intent into the model. It never
 *  drives an LED and it never processes a sample.
 *
 *  Call Process() exactly once per audio callback: DaisyBoonta initialises its
 *  AnalogControl smoothing filters with AudioCallbackRate(), so calling at any
 *  other rate detunes the knob smoothing.
 */
class Controls
{
  public:
    Controls() : hw_(nullptr), state_(nullptr), last_tap_ms_(0) {}

    void Init(daisy::DaisyBoonta* hw, PedalState* state);

    /** Read every input and update the model. Audio-callback context. */
    void Process();

  private:
    void ReadKnobs();
    void ReadToggles();
    void ReadFootswitches();
    void ReadSelects();

    daisy::DaisyBoonta* hw_;
    PedalState*         state_;
    uint32_t            last_tap_ms_;
};

#endif
