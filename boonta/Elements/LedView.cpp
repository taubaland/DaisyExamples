#include "LedView.h"

using namespace daisy;

/** One colour per knob page, shown on the matching select LED. */
static const struct
{
    float r, g, b;
} kPageColour[PedalState::kPageCount] = {
    {0.0f, 0.9f, 0.7f}, // resonator:        teal
    {1.0f, 0.5f, 0.0f}, // exciter levels:   amber
    {0.8f, 0.1f, 1.0f}, // exciter colours:  violet
};

static_assert(PedalState::kPageCount
                  == static_cast<int>(DaisyBoonta::SELECT_LED_LAST),
              "one select LED per knob page");

static constexpr float kIdleBrightness = 0.06f; /**< unselected pages */

/** Gate colour: warm, the colour of something being struck. */
static constexpr float kGateR = 1.0f, kGateG = 0.35f, kGateB = 0.05f;

/** Dim glow on the gate switch while nothing is playing, so the pedal does
 *  not look dead when it is merely quiet. */
static constexpr float kGateIdle = 0.04f;

static constexpr uint32_t kMomentaryFlashMs = 120;

/** Meter fall-off per update; see Clouds' LedView. */
static constexpr float kMeterDecay = 0.993f;

/** How much of the selected page LED's brightness the meter accounts for. */
static constexpr float kMeterDepth = 0.65f;

void LedView::Init(DaisyBoonta* hw)
{
    hw_             = hw;
    meter_          = 0.f;
    relays_known_   = false;
    relays_engaged_ = false;

    hw_->ClearLeds();
    hw_->UpdateLeds();
}

void LedView::Update(const PedalState& state)
{
    const float peak = state.GetOutputLevel();
    meter_ *= kMeterDecay;
    if(peak > meter_)
        meter_ = peak > 1.f ? 1.f : peak;

    DrawGateLed(state);
    DrawBypassLed(state);
    DrawPageLeds(state);
    hw_->UpdateLeds();

    DriveRelays(state);
}

bool LedView::FlashOn(uint32_t period_ms)
{
    return (System::GetNow() % period_ms) < (period_ms / 2);
}

void LedView::DrawGateLed(const PedalState& state)
{
    // Four things to tell apart: nothing playing; a note you are holding,
    // which stops when you step off; a latched gate, which does not; and a
    // note the input played, which is none of your foot's business.
    float level;

    if(state.GateLatched())
        level = 1.f; // latched: steady
    else if(state.Gate())
        level = FlashOn(kMomentaryFlashMs) ? 1.f : 0.25f; // held: pulsing
    else if(state.IsSounding())
        level = 0.5f; // played by the input, or by the drone position
    else
        level = kGateIdle;

    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1,
                          kGateR * level,
                          kGateG * level,
                          kGateB * level);
}

void LedView::DrawBypassLed(const PedalState& state)
{
    // Solid white while the effect is in circuit, off when the relays have
    // routed around it.
    const float on = state.IsActive() ? 1.f : 0.f;
    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, on, on, on);
}

void LedView::DrawPageLeds(const PedalState& state)
{
    for(int i = 0; i < PedalState::kPageCount; i++)
    {
        const bool selected = (i == state.GetPage());

        float r = kPageColour[i].r;
        float g = kPageColour[i].g;
        float b = kPageColour[i].b;

        // The selected page's LED doubles as an output meter.
        float level = kIdleBrightness;
        if(selected)
            level = (1.f - kMeterDepth) + kMeterDepth * meter_;
        else if(!state.SamplesLoaded())
        {
            // The sample data is not in flash, so the sampled strike and
            // blow exciters are silent. The other two LEDs go red and stay
            // red: it is a fault in how the pedal was flashed, not a state
            // it will leave on its own.
            r     = 1.f;
            g     = 0.f;
            b     = 0.f;
            level = FlashOn(600) ? 0.5f : 0.05f;
        }

        hw_->SetSelectLed(static_cast<DaisyBoonta::SelectLed>(i),
                          r * level,
                          g * level,
                          b * level);
    }
}

void LedView::DriveRelays(const PedalState& state)
{
    const bool engage = state.IsActive();

    // Relays click and wear, so only ever write them on a real change.
    if(relays_known_ && engage == relays_engaged_)
        return;

    // Both relays follow `engage`: K2 puts the input jack on the ADC, K1 puts
    // the DAC on the output buffers.
    hw_->relay_output.Write(engage);
    hw_->relay_input.Write(engage);

    relays_engaged_ = engage;
    relays_known_   = true;
}
