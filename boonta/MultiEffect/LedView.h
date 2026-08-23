#pragma once
#ifndef BOONTA_LED_VIEW_H
#define BOONTA_LED_VIEW_H

#include "daisy_boonta.h"
#include "PedalState.h"

/** View.
 *
 *  The only place that touches output hardware: the two footswitch RGB LEDs,
 *  the three select RGB LEDs and the three bypass relays. It reads the model
 *  and never writes it, so re-skinning the pedal can never change its
 *  behaviour.
 *
 *  Each LED reports the switch it sits next to, which is the whole layout in
 *  one sentence: the page footswitch shows the page, the bypass footswitch
 *  shows bypass, and the three select LEDs -- next to the two buttons that
 *  reorder the chain -- show the chain, one LED per position, coloured by the
 *  effect sitting there. Nothing is ever more than a glance away from the
 *  control that changes it.
 *
 *  Call Update() from the main loop, not from the audio callback: it kicks off
 *  an I2C DMA transfer to the LED driver, and the relays are mechanical.
 */
class LedView
{
  public:
    LedView() : hw_(nullptr), relays_engaged_(false), relays_known_(false) {}

    void Init(daisy::DaisyBoonta* hw);

    /** Render the current model state. Main-loop context. */
    void Update(const PedalState& state);

  private:
    void DrawPageLed(const PedalState& state);
    void DrawBypassLed(const PedalState& state);
    void DrawChainLeds(const PedalState& state);
    void DriveRelays(const PedalState& state);

    /** Triangle wave over the given period, 0 to 1 and back. */
    static float Pulse(uint32_t period_ms);

    daisy::DaisyBoonta* hw_;
    bool                relays_engaged_;
    bool                relays_known_; /**< false until the first write */
};

#endif
