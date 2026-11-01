#include "LedView.h"

using namespace daisy;

/** One colour per preset, shown on the matching select LED. */
static const struct
{
    float r, g, b;
} kPresetColour[PedalState::kPresetCount] = {
    {0.f, 0.9f, 0.2f}, // 1: green
    {0.1f, 0.4f, 1.f}, // 2: blue
    {1.f, 0.5f, 0.f},  // 3: amber
};

static_assert(PedalState::kPresetCount
                  == static_cast<int>(DaisyBoonta::SELECT_LED_LAST),
              "one select LED per preset");

static constexpr float    kIdleBrightness = 0.06f; /**< unselected presets */
static constexpr uint32_t kAltFlashMs     = 200;

void LedView::Init(DaisyBoonta* hw)
{
    hw_             = hw;
    relays_known_   = false;
    relays_engaged_ = false;

    hw_->ClearLeds();
    hw_->UpdateLeds();
}

void LedView::Update(const PedalState& state)
{
    DrawFootswitchLeds(state);
    DrawPresetLeds(state);
    hw_->UpdateLeds();

    DriveRelays(state);
}

bool LedView::FlashOn(uint32_t period_ms)
{
    return (System::GetNow() % period_ms) < (period_ms / 2);
}

void LedView::DrawFootswitchLeds(const PedalState& state)
{
    // Left footswitch LED: solid white while the effect is in circuit.
    const float on = state.IsActive() ? 1.f : 0.f;
    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1, on, on, on);

    // Right footswitch LED: flashes while the alt switch is held, otherwise
    // blinks the tapped tempo so you can see what you dialled in.
    if(state.AltHeld())
    {
        const float f = FlashOn(kAltFlashMs) ? 1.f : 0.f;
        hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, f, f, 0.f);
    }
    else
    {
        const uint32_t period
            = static_cast<uint32_t>(state.GetTapIntervalMs());
        const float f = (period > 0 && FlashOn(period)) ? 0.8f : 0.f;
        hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, 0.f, f, 0.f);
    }
}

void LedView::DrawPresetLeds(const PedalState& state)
{
    for(int i = 0; i < PedalState::kPresetCount; i++)
    {
        const bool  selected = (i == state.GetPreset());
        const float level    = selected ? 1.f : kIdleBrightness;

        hw_->SetSelectLed(static_cast<DaisyBoonta::SelectLed>(i),
                          kPresetColour[i].r * level,
                          kPresetColour[i].g * level,
                          kPresetColour[i].b * level);
    }
}

void LedView::DriveRelays(const PedalState& state)
{
    const bool engage = state.IsActive();

    // Relays click and wear, so only ever write them on a real change.
    if(relays_known_ && engage == relays_engaged_)
        return;

    hw_->relay_input.Write(engage);
    hw_->relay_output.Write(engage);
    hw_->relay_bypass.Write(!engage);

    relays_engaged_ = engage;
    relays_known_   = true;
}
