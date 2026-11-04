#pragma once
#ifndef BOONTA_LED_VIEW_H
#define BOONTA_LED_VIEW_H

#include "BoontaDuino.h"
#include "PedalState.h"

/** View.
 *
 *  The only place that touches output hardware: the two footswitch RGB LEDs,
 *  the three select RGB LEDs and the three bypass relays. It reads the model
 *  and never writes it, so re-skinning the pedal can never change its
 *  behaviour.
 *
 *  Call Update() from loop(), not from the audio callback: it pushes 15
 *  channels over a blocking Wire transfer, and the relays are mechanical.
 */
class LedView
{
  public:
    LedView() : hw_(nullptr), relays_engaged_(false), relays_known_(false) {}

    void Init(BoontaDuino* hw);

    /** Render the current model state. Main-loop context. */
    void Update(const PedalState& state);

  private:
    void DrawFootswitchLeds(const PedalState& state);
    void DrawPresetLeds(const PedalState& state);
    void DriveRelays(const PedalState& state);

    /** True during the 'on' half of a flash cycle of the given period. */
    static bool FlashOn(uint32_t period_ms);

    BoontaDuino* hw_;
    bool         relays_engaged_;
    bool         relays_known_; /**< false until the first write */
};

#endif
