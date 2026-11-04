#include "LedView.h"

using namespace daisy;

/** One colour per playback mode, shown on the matching select LED. */
static const struct
{
    float r, g, b;
} kModeColour[PedalState::kModeCount] = {
    {0.8f, 0.1f, 1.0f}, // granular: violet
    {0.0f, 0.9f, 0.7f}, // stretch:  teal
    {1.0f, 0.5f, 0.0f}, // delay:    amber
};

static_assert(PedalState::kModeCount
                  == static_cast<int>(DaisyBoonta::SELECT_LED_LAST),
              "one select LED per playback mode");

static constexpr float kIdleBrightness = 0.06f; /**< unselected modes */

/** Freeze colour: cold, and nothing else on the pedal is this colour. */
static constexpr float kFreezeR = 0.15f, kFreezeG = 0.65f, kFreezeB = 1.0f;

/** Dim glow on the freeze switch while the buffer is running, so the pedal
 *  does not look dead when it is working perfectly well. */
static constexpr float kFreezeIdle = 0.04f;

static constexpr uint32_t kMomentaryFlashMs = 120;

/** Meter fall-off per update. Updates run about once a millisecond, so this
 *  takes roughly a fifth of a second to drop from full scale to nothing --
 *  slow enough to see, fast enough to follow a grain cloud thinning out. */
static constexpr float kMeterDecay = 0.993f;

/** How much of the selected mode LED's brightness the meter accounts for.
 *  The rest is a floor, so the selected mode is always legible in silence. */
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

    DrawFreezeLed(state);
    DrawBypassLed(state);
    DrawModeLeds(state);
    hw_->UpdateLeds();

    DriveRelays(state);
}

bool LedView::FlashOn(uint32_t period_ms)
{
    return (System::GetNow() % period_ms) < (period_ms / 2);
}

void LedView::DrawFreezeLed(const PedalState& state)
{
    // Three things to tell apart, and the distinction that matters on stage
    // is latched from held: a latched freeze is going to stay there when you
    // take your foot off, a held one is not.
    float level;

    if(!state.IsFrozen())
        level = kFreezeIdle;
    else if(state.FreezeLatched())
        level = 1.f; // latched: steady, it will stay when you step off
    else
        level = FlashOn(kMomentaryFlashMs) ? 1.f : 0.25f; // held: pulsing

    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1,
                          kFreezeR * level,
                          kFreezeG * level,
                          kFreezeB * level);
}

void LedView::DrawBypassLed(const PedalState& state)
{
    // Solid white while the effect is in circuit, off when the relays have
    // routed around it.
    const float on = state.IsActive() ? 1.f : 0.f;
    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, on, on, on);
}

void LedView::DrawModeLeds(const PedalState& state)
{
    for(int i = 0; i < PedalState::kModeCount; i++)
    {
        const bool selected = (i == static_cast<int>(state.GetMode()));

        // The selected mode's LED doubles as an output meter: it breathes
        // with the cloud, which is the quickest way to tell a buffer that has
        // nothing in it from a mix knob that is turned down.
        float level = kIdleBrightness;
        if(selected)
            level = (1.f - kMeterDepth) + kMeterDepth * meter_;

        hw_->SetSelectLed(static_cast<DaisyBoonta::SelectLed>(i),
                          kModeColour[i].r * level,
                          kModeColour[i].g * level,
                          kModeColour[i].b * level);
    }
}

void LedView::DriveRelays(const PedalState& state)
{
    const bool engage = state.IsActive();

    // Relays click and wear, so only ever write them on a real change.
    if(relays_known_ && engage == relays_engaged_)
        return;

    // Both relays follow `engage`: K2 puts the input jack on the ADC, K1 puts
    // the DAC on the output buffers. This used to drive a third relay that
    // does not exist, and to drive K2 inverted under the name relay_bypass --
    // which de-energised it exactly when the pedal was supposed to be in
    // circuit, disconnecting the input. That is why the input measured dead.
    hw_->relay_output.Write(engage);
    hw_->relay_input.Write(engage);

    relays_engaged_ = engage;
    relays_known_   = true;
}
