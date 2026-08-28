#include "LedView.h"

#include "EffectRegistry.h"

using namespace daisy;

/** One colour per page. The first three are also the colours of the three
 *  effects wherever they appear, so "teal is the EQ" is learned once and holds
 *  on the page footswitch and in the chain display alike.
 *
 *  Each effect is one of the three secondaries -- two channels full on, the
 *  third off -- which keeps them maximally distinct from each other and from
 *  the meta page's white, and means none of them can be confused with the
 *  green/red the bypass footswitch uses.
 *
 *  Do not "fix" DaisyBoonta::SetSelectLed() to make these work. It looks wrong:
 *  it feeds the caller's green into the channel named _B and blue into the one
 *  named _G, and maps SELECT_LED_1 onto the channels named SELECT_2. Both are
 *  deliberate. The _G/_B names in that enum are reversed with respect to how
 *  the LEDs are actually wired, and the index order is 2/1/3 left to right, so
 *  the setter's swaps are what make it come out right. Verified on hardware:
 *  passing these triples lights the row teal, gold, purple from the left.
 *  Straightening out the setter would silently exchange drive and reverb. */
static const struct
{
    float r, g, b;
} kPageColour[PedalState::PAGE_LAST] = {
    {1.0f, 1.0f, 1.0f}, // unused: slot pages take their colour from the effect
    {1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f}, // Meta : white
};

/** A page's colour. The three slot pages borrow the colour of whatever effect
 *  is in them, so swapping an effect swaps its colour everywhere at once; the
 *  meta page edits the chain rather than an effect, so it keeps its own. */
static void PageColour(const PedalState& state,
                       PedalState::Page  page,
                       float&            r,
                       float&            g,
                       float&            b)
{
    const PedalState::Slot slot = PedalState::PageSlot(page);
    if(slot == PedalState::SLOT_LAST)
    {
        r = kPageColour[PedalState::PAGE_META].r;
        g = kPageColour[PedalState::PAGE_META].g;
        b = kPageColour[PedalState::PAGE_META].b;
        return;
    }
    const EffectDesc& d = effects::Get(state.GetSlotEffect(slot))->Desc();
    r = d.r;
    g = d.g;
    b = d.b;
}

/** The chain display leans on the page colours, which only works while the
 *  slots and the first three pages are in the same order. */
static_assert(static_cast<int>(PedalState::SLOT_EQ)
                  == static_cast<int>(PedalState::PAGE_EQ),
              "slot colours are page colours");
static_assert(static_cast<int>(PedalState::SLOT_DRIVE)
                  == static_cast<int>(PedalState::PAGE_DRIVE),
              "slot colours are page colours");
static_assert(static_cast<int>(PedalState::SLOT_REVERB)
                  == static_cast<int>(PedalState::PAGE_REVERB),
              "slot colours are page colours");

static_assert(static_cast<int>(PedalState::SLOT_LAST)
                  == static_cast<int>(DaisyBoonta::SELECT_LED_LAST),
              "one select LED per chain position");

/** Brightness for a bypassed effect. Dim rather than dark: the chain order is
 *  still worth reading at a glance even where an effect is switched out. */
static constexpr float kBypassedLevel = 0.20f;

/** How far the page LED dips while knobs are still parked. */
static constexpr float    kParkedFloor  = 0.15f;
static constexpr uint32_t kParkedPeriod = 700;

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
    DrawPageLed(state);
    DrawBypassLed(state);
    DrawChainLeds(state);
    hw_->UpdateLeds();

    DriveRelays(state);
}

float LedView::Pulse(uint32_t period_ms)
{
    const uint32_t half  = period_ms / 2;
    const uint32_t phase = System::GetNow() % period_ms;

    const float up = static_cast<float>(phase) / static_cast<float>(half);
    return phase < half ? up : 2.f - up;
}

void LedView::DrawPageLed(const PedalState& state)
{
    const PedalState::Page page = state.GetPage();

    // Breathe until something on this page has actually been changed, then go
    // solid. The signal is "nothing here has been touched since you arrived",
    // which is also the state in which turning a knob appears to do nothing,
    // because every knob is still parked.
    //
    // Two conditions were tried and discarded. "Any knob still parked" is true
    // on almost every page almost always -- you rarely sweep all six -- so the
    // LED pulsed permanently and said nothing. "Any knob picked up" clears the
    // moment a pot happens to sit on its stored value, which can happen on the
    // first block after arriving, without anything having been edited.
    const float level = state.PageEdited()
                            ? 1.f
                            : kParkedFloor
                                  + (1.f - kParkedFloor) * Pulse(kParkedPeriod);

    float r, g, b;
    PageColour(state, page, r, g, b);

    hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_1,
                          r * level,
                          g * level,
                          b * level);
}

void LedView::DrawBypassLed(const PedalState& state)
{
    // Green in circuit, red out. Readable at a glance from standing height,
    // which a brightness difference on a single colour is not.
    if(state.IsActive())
        hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, 0.f, 1.f, 0.f);
    else
        hw_->SetFootSwitchLed(DaisyBoonta::FOOTSWITCH_LED_2, 1.f, 0.f, 0.f);
}

void LedView::DrawChainLeds(const PedalState& state)
{
    for(int position = 0; position < PedalState::SLOT_LAST; position++)
    {
        const PedalState::Slot slot = state.SlotAt(position);

        // Full brightness in circuit, a fifth of it when switched out. Same
        // colour either way, so the order stays readable while the state of
        // each effect is obvious.
        const float level
            = state.SlotBypassed(slot) ? kBypassedLevel : 1.f;

        const EffectDesc& d = effects::Get(state.GetSlotEffect(slot))->Desc();

        hw_->SetSelectLed(static_cast<DaisyBoonta::SelectLed>(position),
                          d.r * level,
                          d.g * level,
                          d.b * level);
    }
}

void LedView::DriveRelays(const PedalState& state)
{
    const bool engage = state.IsActive();

    // Relays click and wear, so only ever write them on a real change.
    if(relays_known_ && engage == relays_engaged_)
        return;

    // All three relays follow `engage`. The Template drove the bypass relay
    // inverted -- energised when the pedal was *out* of circuit -- and that is
    // backwards for this board.
    //
    // Measured with a guitar in the pedal and its output captured, averaged
    // over interleaved trials so playing dynamics cancel: with input and output
    // engaged, bypass=1 gives +13.3 dB out for -28.5 dB in, while bypass=0
    // gives only +4.5 dB. De-energised, the bypass relay *closes* an analog
    // path straight from input jack to output jack, so the dry signal sums with
    // the DSP output and partially cancels it. Energising it opens that path
    // and leaves the Daisy alone in circuit.
    //
    // So (1,1,1) is the effect in circuit and (0,0,0) is true bypass, passing
    // the input to the output through that same analog path with the Daisy
    // disconnected. This is also exactly what HardwareTest does.
    hw_->relay_input.Write(engage);
    hw_->relay_output.Write(engage);
    hw_->relay_bypass.Write(engage);

    relays_engaged_ = engage;
    relays_known_   = true;
}
