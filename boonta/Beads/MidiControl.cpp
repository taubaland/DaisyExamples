#include "MidiControl.h"

#include "MidiMap.h"

using namespace daisy;

void MidiControl::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_    = hw;
    state_ = state;

    // The board Init()s this handler but never starts it, so nothing arrives
    // on the UART until StartReceive(). DaisyBoonta::InitMidi() takes
    // libDaisy's defaults: USART_1 on PB7 rx / PB6 tx. If your MIDI jack is
    // wired to different Seed pins, re-Init the handler with a config of your
    // own here before starting it.
    hw_->midi.StartReceive();

    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::INTERNAL;

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
            // Program change selects the pitch quantisation. A footswitch
            // controller sends these, and quantisation is the only thing on
            // this pedal that is a choice from a short list.
            const auto pc = event.AsProgramChange();
            if(pc.program < PedalState::kQuantiseCount)
            {
                state_->SetQuantise(pc.program);
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
            state_->SetParam(static_cast<PedalState::Param>(b.index),
                             midimap::Normalise(value));
            break;

        case midimap::Target::TOGGLE:
            state_->SetToggle(static_cast<PedalState::Toggle>(b.index),
                              static_cast<PedalState::TogglePos>(
                                  midimap::Quantise(value, 3)));
            break;

        case midimap::Target::QUANTISE:
            state_->SetQuantise(
                midimap::Quantise(value, PedalState::kQuantiseCount));
            break;

        case midimap::Target::FREEZE:
            // A separate flag from the footswitch's, not the same one: the
            // model ORs the two, so a controller holding freeze cannot be
            // undone by the switch sitting up, and the switch still works
            // while a controller is connected.
            state_->SetFreezeRemote(midimap::IsOn(value));
            break;

        case midimap::Target::BYPASS:
            state_->SetBypass(!midimap::IsOn(value));
            break;

        default: break;
    }
}
