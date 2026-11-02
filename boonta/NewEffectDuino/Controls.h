#pragma once
#ifndef BOONTA_CONTROLS_H
#define BOONTA_CONTROLS_H

#include "BoontaDuino.h"
#include "PedalState.h"

/** Controller.
 *
 *  The only place that touches input hardware. It reads knobs, toggles,
 *  footswitches and expression, turns them into intent (bypass toggled, preset
 *  changed, drive set to 0.6) and writes that intent into the model. It never
 *  drives an LED and it never processes a sample.
 *
 *  Call Process() exactly once per audio callback: the AnalogControl smoothing
 *  filters and the Switch debouncing are both initialised with the rate passed
 *  to BoontaDuino::Init(), so calling at any other rate detunes them.
 */
class Controls
{
  public:
    Controls() : hw_(nullptr), state_(nullptr), last_tap_ms_(0) {}

    void Init(BoontaDuino* hw, PedalState* state);

    /** Read every input and update the model. Audio-callback context. */
    void Process();

  private:
    void ReadKnobs();
    void ReadToggles();
    void ReadFootswitches();
    void ReadSelects();

    BoontaDuino* hw_;
    PedalState*  state_;
    uint32_t     last_tap_ms_;
};

#endif
