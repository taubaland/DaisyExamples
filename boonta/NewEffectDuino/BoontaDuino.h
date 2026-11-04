#pragma once
#ifndef BOONTA_DUINO_H
#define BOONTA_DUINO_H

#include "DaisyDuino.h"

/** Board support for the Boonta pedal under DaisyDuino.
 *
 *  The Arduino-side stand-in for libDaisy's DaisyBoonta. DaisyDuino only knows
 *  the official Electrosmith boards, so the Boonta's knobs, toggles,
 *  footswitches, relays and LED driver are wired up here instead.
 *
 *  The enum names match DaisyBoonta's deliberately, so Controls.cpp and
 *  LedView.cpp read the same in both builds and can be diffed against each
 *  other.
 *
 *  What this does NOT bring over from DaisyBoonta: MIDI (D13/D14), QSPI
 *  storage, and the audio setup, which DaisyDuino's DAISY object owns.
 */
class BoontaDuino
{
  public:
    enum Knob
    {
        KNOB_1,
        KNOB_2,
        KNOB_3,
        KNOB_4,
        KNOB_5,
        KNOB_6,
        KNOB_LAST,
    };

    enum TogSw
    {
        TOG_SW_1,
        TOG_SW_2,
        TOG_SW_3,
        TOG_SW_LAST,
    };

    enum Sw
    {
        SW_SEL_1,
        SW_SEL_2,
        SW_FS_1,
        SW_FS_2,
        SW_LAST,
    };

    enum FootSwitchLed
    {
        FOOTSWITCH_LED_1,
        FOOTSWITCH_LED_2,
        FOOTSWITCH_LED_LAST,
    };

    enum SelectLed
    {
        SELECT_LED_1,
        SELECT_LED_2,
        SELECT_LED_3,
        SELECT_LED_LAST,
    };

    /** Three-way toggle positions, matching daisy::Switch3's values so the
     *  mapping in Controls.cpp is unchanged. */
    enum TogglePosition
    {
        POS_CENTER = 0,
        POS_UP     = 1,
        POS_DOWN   = 2,
    };

    BoontaDuino() {}

    /** \param control_rate  Rate at which ProcessAllControls() will be called,
     *                       in Hz. Use DAISY.AudioCallbackRate(): it sets both
     *                       the knob smoothing and the switch debounce timing. */
    void Init(float control_rate);

    /** Debounce the switches and filter the knobs. Call once per audio
     *  callback, at the rate given to Init(). */
    void ProcessAllControls();

    float GetKnobValue(Knob k);
    float GetExpression();

    /** Live read, no debouncing -- the toggles don't bounce in a way that
     *  matters because nothing edge-triggers on them. */
    int GetSwitchPosition(TogSw s);

    void SetFootSwitchLed(FootSwitchLed idx, float r, float g, float b);
    void SetSelectLed(SelectLed idx, float r, float g, float b);
    void ClearLeds();
    void UpdateLeds();

    void WriteRelayInput(bool on);
    void WriteRelayOutput(bool on);
    void WriteRelayBypass(bool on);

    daisy::Switch switches[SW_LAST];

  private:
    daisy::AnalogControl             knob_[KNOB_LAST];
    daisy::AnalogControl             expression_;
    daisy::LedDriverPca9685<1, true> led_driver_;
};

#endif
