#include "BoontaDuino.h"

#include <Wire.h>

using namespace daisy;

// --- Pins ------------------------------------------------------------------
//
// From libDaisy/src/daisy_boonta.cpp, translated through the Daisy Seed pin
// table in daisy_seed.h. The STM32 core's DAISY_SEED variant happens to number
// its pins the same way Daisy does -- PC11 is pin 1, which is Daisy's D1 -- so
// these line up with the libDaisy source one for one.

// Three-way toggles: two pins each, both pulled up.
static constexpr uint8_t kTogglePins[BoontaDuino::TOG_SW_LAST][2] = {
    {PC11, PC10}, // TOG_SW_1: D1 / D2
    {PC9, PC8},   // TOG_SW_2: D3 / D4
    {PD2, PC12},  // TOG_SW_3: D5 / D6
};

// Momentary switches, active low.
static constexpr uint8_t kSwitchPins[BoontaDuino::SW_LAST] = {
    PG10, // SW_SEL_1: D7
    PG11, // SW_SEL_2: D8
    PB4,  // SW_FS_1:  D9
    PB5,  // SW_FS_2:  D10
};

/** Knob enum -> analog pin.
 *
 *  These are deliberately back to front, because libDaisy's are:
 *  InitAnalogControls() assigns ADC channel 0 to PIN_KNOB_6 and channel 5 to
 *  PIN_KNOB_1, so GetKnobValue(KNOB_1) has always read the pot on D21, not
 *  D16. Reproduced rather than corrected -- whether the pots are wired
 *  backwards on the board or this is an old mistake, the two builds have to
 *  agree or the same patch sounds different on each. See PORTING.md.
 */
static constexpr uint8_t kKnobPins[BoontaDuino::KNOB_LAST] = {
    PC4, // KNOB_1: D21
    PC1, // KNOB_2: D20
    PA6, // KNOB_3: D19
    PA7, // KNOB_4: D18
    PB1, // KNOB_5: D17
    PA3, // KNOB_6: D16
};

static constexpr uint8_t kExpressionPin = PC0; // D15

static constexpr uint8_t kRelayOutputPin = PA5; // D22
static constexpr uint8_t kRelayBypassPin = PA4; // D23
static constexpr uint8_t kRelayInputPin  = PA1; // D24

// --- LEDs ------------------------------------------------------------------
//
// One PCA9685 at 7-bit address 0x40 (base 0b01000000 | 0x00), on I2C1. The
// DAISY_SEED variant's default Wire pins are PB9/PB8, which is the same I2C1
// the libDaisy build configures, so Wire needs no re-pinning.
//
// The select LEDs are not in channel order: 1 is at 3-5, 3 is at 6-8 and 2 is
// at 9-11. Taken from the address tables in DaisyBoonta::SetSelectLed() rather
// than from the LedOrder enum, which names those channels R, B, G and then
// swaps g and b again at the call site -- two inversions that cancel.

static constexpr uint8_t kFootSwitchLedBase[BoontaDuino::FOOTSWITCH_LED_LAST]
    = {0, 12};

static constexpr uint8_t kSelectLedBase[BoontaDuino::SELECT_LED_LAST]
    = {3, 9, 6};

static constexpr uint8_t kLedDriverAddress[1] = {0x00};

static daisy::LedDriverPca9685<1, true>::DmaBuffer led_buffer_a, led_buffer_b;

// --- Lifecycle -------------------------------------------------------------

void BoontaDuino::Init(float control_rate)
{
    for(size_t i = 0; i < TOG_SW_LAST; i++)
    {
        pinMode(kTogglePins[i][0], INPUT_PULLUP);
        pinMode(kTogglePins[i][1], INPUT_PULLUP);
    }

    for(size_t i = 0; i < SW_LAST; i++)
        switches[i].Init(control_rate, true, kSwitchPins[i], INPUT_PULLUP);

    for(size_t i = 0; i < KNOB_LAST; i++)
        knob_[i].Init(kKnobPins[i], control_rate);

    expression_.Init(kExpressionPin, control_rate);

    pinMode(kRelayOutputPin, OUTPUT);
    pinMode(kRelayBypassPin, OUTPUT);
    pinMode(kRelayInputPin, OUTPUT);

    led_driver_.Init(kLedDriverAddress, led_buffer_a, led_buffer_b);
    ClearLeds();
    UpdateLeds();
}

void BoontaDuino::ProcessAllControls()
{
    for(size_t i = 0; i < SW_LAST; i++)
        switches[i].Debounce();

    for(size_t i = 0; i < KNOB_LAST; i++)
        knob_[i].Process();

    expression_.Process();
}

// --- Inputs ----------------------------------------------------------------

float BoontaDuino::GetKnobValue(Knob k)
{
    return k < KNOB_LAST ? knob_[k].Value() : 0.f;
}

float BoontaDuino::GetExpression()
{
    return expression_.Value();
}

int BoontaDuino::GetSwitchPosition(TogSw s)
{
    const size_t idx = s < TOG_SW_LAST ? s : TOG_SW_1;

    // Both pins are pulled up, so the active side reads low. Matches
    // daisy::Switch3::Read().
    if(!digitalRead(kTogglePins[idx][0]))
        return POS_UP;
    if(!digitalRead(kTogglePins[idx][1]))
        return POS_DOWN;

    return POS_CENTER;
}

// --- Outputs ---------------------------------------------------------------

void BoontaDuino::SetFootSwitchLed(FootSwitchLed idx, float r, float g, float b)
{
    if(idx >= FOOTSWITCH_LED_LAST)
        return;

    const uint8_t base = kFootSwitchLedBase[idx];
    led_driver_.SetLed(base + 0, r);
    led_driver_.SetLed(base + 1, g);
    led_driver_.SetLed(base + 2, b);
}

void BoontaDuino::SetSelectLed(SelectLed idx, float r, float g, float b)
{
    if(idx >= SELECT_LED_LAST)
        return;

    const uint8_t base = kSelectLedBase[idx];
    led_driver_.SetLed(base + 0, r);
    led_driver_.SetLed(base + 1, g);
    led_driver_.SetLed(base + 2, b);
}

void BoontaDuino::ClearLeds()
{
    for(size_t i = 0; i < FOOTSWITCH_LED_LAST; i++)
        SetFootSwitchLed(static_cast<FootSwitchLed>(i), 0.f, 0.f, 0.f);

    for(size_t i = 0; i < SELECT_LED_LAST; i++)
        SetSelectLed(static_cast<SelectLed>(i), 0.f, 0.f, 0.f);
}

void BoontaDuino::UpdateLeds()
{
    // Wire is blocking, so unlike the libDaisy build this transfer costs real
    // time in the caller. Main loop only -- never the audio callback.
    led_driver_.SwapBuffersAndTransmit();
}

void BoontaDuino::WriteRelayInput(bool on)
{
    digitalWrite(kRelayInputPin, on ? HIGH : LOW);
}

void BoontaDuino::WriteRelayOutput(bool on)
{
    digitalWrite(kRelayOutputPin, on ? HIGH : LOW);
}

void BoontaDuino::WriteRelayBypass(bool on)
{
    digitalWrite(kRelayBypassPin, on ? HIGH : LOW);
}
