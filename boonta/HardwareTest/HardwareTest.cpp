// # Boonta Hardware Test
//
// ## Controls
// | Control       | Function                                          |
// |---------------|---------------------------------------------------|
// | KNOB_1        | Red component for all select LEDs                 |
// | KNOB_2        | Green component for all select LEDs               |
// | KNOB_3        | Blue component for all select LEDs                |
// | KNOB_4        | Brightness for Select LED 1                       |
// | KNOB_5        | Brightness for Select LED 2                       |
// | KNOB_6        | Brightness for Select LED 3                       |
// | TOG_SW_1      | Flash rate for Select LED 1 (center=off,up=slow,down=fast) |
// | TOG_SW_2      | Flash rate for Select LED 2 (center=off,up=slow,down=fast) |
// | TOG_SW_3      | Flash rate for Select LED 3 (center=off,up=slow,down=fast) |
// | SW_SEL_1      | Cycles select LEDs: normal → all full → all off   |
// | SW_FS_1       | Momentary: left footswitch LED on while held      |
// | SW_FS_2       | Toggle: right footswitch LED + all relays         |

#include "daisy_boonta.h"

using namespace daisy;

DaisyBoonta hw;

// Flash period in ms for each switch rate
static constexpr uint32_t kSlowPeriodMs = 1000; // 1 Hz
static constexpr uint32_t kFastPeriodMs = 125;  // 8 Hz

// Override state for select button: 0=normal, 1=all full, 2=all off
static uint8_t sel_override = 0;

// Toggle state for right footswitch LED + relays
static bool fs2_toggled = false;

// Relay state
static bool relay_state = false;

// Returns true during the 'on' half of the flash cycle for the given period
static bool FlashOn(uint32_t period_ms)
{
    return (System::GetNow() % period_ms) < (period_ms / 2);
}

// Returns whether the LED should illuminate based on the switch position
static bool LedActive(int sw_pos)
{
    switch(sw_pos)
    {
        case Switch3::POS_UP:   return FlashOn(kSlowPeriodMs);
        case Switch3::POS_DOWN: return FlashOn(kFastPeriodMs);
        default: return false; // POS_CENTER = off
    }
}

int main(void)
{
    hw.Init();
    hw.StartAdc();

    while(1)
    {
        hw.ProcessAllControls();

        // --- Knob readings ---
        float r   = hw.GetKnobValue(DaisyBoonta::KNOB_1);
        float g   = hw.GetKnobValue(DaisyBoonta::KNOB_2);
        float b   = hw.GetKnobValue(DaisyBoonta::KNOB_3);
        float br1 = hw.GetKnobValue(DaisyBoonta::KNOB_4);
        float br2 = hw.GetKnobValue(DaisyBoonta::KNOB_5);
        float br3 = hw.GetKnobValue(DaisyBoonta::KNOB_6);

        // --- Select button: cycle override state ---
        if(hw.switches[DaisyBoonta::SW_SEL_1].RisingEdge())
            sel_override = (sel_override + 1) % 3;

        // --- Right footswitch: toggle LED + relays on rising edge ---
        if(hw.switches[DaisyBoonta::SW_FS_2].RisingEdge())
        {
            fs2_toggled = !fs2_toggled;
            relay_state = !relay_state;
            hw.relay_output.Write(relay_state);
            hw.relay_bypass.Write(relay_state);
            hw.relay_input.Write(relay_state);
        }

        // --- Select LED flash rates from toggle switches ---
        int sw1_pos = hw.GetSwitchPosition(DaisyBoonta::TOG_SW_1);
        int sw2_pos = hw.GetSwitchPosition(DaisyBoonta::TOG_SW_2);
        int sw3_pos = hw.GetSwitchPosition(DaisyBoonta::TOG_SW_3);

        bool led1_on = LedActive(sw1_pos);
        bool led2_on = LedActive(sw2_pos);
        bool led3_on = LedActive(sw3_pos);

        // --- Apply select override ---
        if(sel_override == 1)
        {
            // All select LEDs full white at max brightness
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_1, 1.f, 1.f, 1.f);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_2, 1.f, 1.f, 1.f);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_3, 1.f, 1.f, 1.f);
        }
        else if(sel_override == 2)
        {
            // All select LEDs off
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_1, 0.f, 0.f, 0.f);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_2, 0.f, 0.f, 0.f);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_3, 0.f, 0.f, 0.f);
        }
        else
        {
            // Normal: color from knobs 1-3, brightness from knobs 4-6,
            // gated by flash state from toggle switches
            float s1 = led1_on ? br1 : 0.f;
            float s2 = led2_on ? br2 : 0.f;
            float s3 = led3_on ? br3 : 0.f;

            hw.SetSelectLed(DaisyBoonta::SELECT_LED_1, r * s1, g * s1, b * s1);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_2, r * s2, g * s2, b * s2);
            hw.SetSelectLed(DaisyBoonta::SELECT_LED_3, r * s3, g * s3, b * s3);
        }

        // --- Left footswitch: momentary white on FOOTSWITCH_LED_1 ---
        if(hw.switches[DaisyBoonta::SW_FS_1].Pressed())
            hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1, 1.f, 1.f, 1.f);
        else
            hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1, 0.f, 0.f, 0.f);

        // --- Right footswitch LED: toggle green ---
        if(fs2_toggled)
            hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, 0.f, 1.f, 0.f);
        else
            hw.SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, 0.f, 0.f, 0.f);

        hw.UpdateLeds();

        // Small delay to keep loop rate stable (~1kHz)
        System::Delay(1);
    }
}
