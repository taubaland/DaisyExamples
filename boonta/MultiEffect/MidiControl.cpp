#include "MidiControl.h"

using namespace daisy;

/** Where the MIDI input jack is wired.
 *
 *  Off by default, in which case the board's own choice stands: USART_1 on
 *  PB7 rx / PB6 tx, which is what libDaisy defaults to and what
 *  DaisyBoonta::InitMidi() accepts without comment. Nothing in this repository
 *  confirms that is where the Boonta's jack actually goes -- the board file has
 *  already proved wrong twice, on the select LED channel names and on the
 *  bypass relay polarity -- so this is here to make the alternative one edit
 *  rather than a hunt.
 *
 *  Daisy Seed pin to port/pin: D13 = PB6, D14 = PB7, D9 = PG14, D10 = PG13.
 */
static constexpr bool kUseCustomUartPins = false;
static constexpr UartHandler::Config::Peripheral kUartPeriph
    = UartHandler::Config::Peripheral::USART_1;
static constexpr Pin kUartRxPin = Pin(PORTB, 7);
static constexpr Pin kUartTxPin = Pin(PORTB, 6);

void MidiControl::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_    = hw;
    state_ = state;

    // The board Init()s this handler but never starts it, so nothing has ever
    // arrived on the UART -- this is the first build in which UART MIDI could
    // have worked at all.
    //
    // DaisyBoonta::InitMidi() takes libDaisy's defaults: USART_1 on PB7 rx /
    // PB6 tx. Verified at register level that those pins are in
    // alternate-function mode, the receiver is enabled and acknowledged, and
    // the baud divisor is 31250 -- and that the line has never seen a
    // transition. If your MIDI jack is wired to different Seed pins, that is
    // what this re-initialises; set kUseCustomUartPins and edit the two below.
    if(kUseCustomUartPins)
    {
        MidiUartHandler::Config cfg;
        cfg.transport_config.periph = kUartPeriph;
        cfg.transport_config.rx     = kUartRxPin;
        cfg.transport_config.tx     = kUartTxPin;
        hw_->midi.Init(cfg);
    }

    hw_->midi.StartReceive();

    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph
        = MidiUsbTransport::Config::INTERNAL;
    usb_.Init(usb_config);
    usb_.StartReceive();
}

void MidiControl::Process()
{
    hw_->midi.Listen();
    while(hw_->midi.HasEvents())
    {
        received_uart_++;
        Handle(hw_->midi.PopEvent());
    }

    usb_.Listen();
    while(usb_.HasEvents())
    {
        received_usb_++;
        Handle(usb_.PopEvent());
    }
}

int MidiControl::TakePresetRequest()
{
    const int req   = preset_request_;
    preset_request_ = -1;
    return req;
}

void MidiControl::Handle(MidiEvent event)
{
    if(midimap::kChannel != midimap::kOmniChannel
       && event.channel != midimap::kChannel)
        return;

    switch(event.type)
    {
        case ControlChange:
        {
            const auto cc = event.AsControlChange();
            ApplyControlChange(cc.control_number, cc.value);
            break;
        }
        case ProgramChange:
        {
            const auto pc = event.AsProgramChange();
            if(pc.program < SavedBank::kPresetCount)
            {
                preset_request_ = pc.program;
                accepted_++;
            }
            break;
        }
        default: break;
    }
}

void MidiControl::ApplyControlChange(uint8_t cc, uint8_t value)
{
    const midimap::Binding b = midimap::Decode(cc);

    if(b.target == midimap::Target::NONE)
        return; // not ours; most controllers are not

    accepted_++;

    switch(b.target)
    {
        case midimap::Target::PARAM:
            state_->SetKnob(static_cast<PedalState::Page>(b.page),
                            b.knob,
                            midimap::Normalise(value));

            // A parameter has genuinely changed, so the page LED should say so
            // -- but only when it is this page that moved.
            if(b.page == state_->GetPage())
                state_->SetPageEdited(true);

            // Nothing parks the pot here. Controls owns that decision and
            // handles it: a knob that is picked up keeps writing its parameter,
            // so a remote change to a live knob is overwritten on the next
            // block. That is the right way round -- the hand on the pedal wins
            // over the controller across the room, and moving nothing leaves
            // the remote value standing.
            break;

        case midimap::Target::MASTER_BYPASS:
            state_->SetBypass(!midimap::IsOn(value));
            break;

        case midimap::Target::SLOT_BYPASS:
            state_->SetSlotBypass(static_cast<PedalState::Slot>(b.slot),
                                  !midimap::IsOn(value));
            break;

        case midimap::Target::CHAIN_ORDER:
            state_->SetOrder(midimap::Quantise(value, PedalState::kOrderCount));
            break;

        case midimap::Target::PAGE_SELECT:
            state_->SetPage(static_cast<PedalState::Page>(
                midimap::Quantise(value, PedalState::PAGE_LAST)));
            break;

        default: break;
    }
}
