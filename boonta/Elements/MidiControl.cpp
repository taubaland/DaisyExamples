#include "MidiControl.h"

#include "MidiMap.h"

using namespace daisy;

void MidiControl::Init(DaisyBoonta* hw, PedalState* state)
{
    hw_    = hw;
    state_ = state;

    // The board Init()s this handler but never starts it, so nothing arrives
    // on the UART until StartReceive(). DaisyBoonta::InitMidi() takes
    // libDaisy's defaults: USART_1 on PB7 rx / PB6 tx.
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
        case NoteOn:
        {
            const auto n = event.AsNoteOn();
            ApplyNote(n.note, n.velocity, true);
            break;
        }

        case NoteOff:
        {
            const auto n = event.AsNoteOff();
            ApplyNote(n.note, n.velocity, false);
            break;
        }

        case PitchBend:
        {
            // Fourteen bits, centred on zero.
            const auto pb = event.AsPitchBend();
            state_->SetBend(static_cast<float>(pb.value) / 8192.f);
            accepted_++;
            break;
        }

        case ControlChange:
        {
            const auto cc = event.AsControlChange();
            ApplyControlChange(cc.control_number, cc.value);
            break;
        }

        case ChannelMode:
            // All Notes Off and its neighbours. Whichever one it is, nothing
            // should still be sounding on a key afterwards.
            state_->AllNotesOff();
            accepted_++;
            break;

        case ProgramChange:
        {
            // Program change selects the knob page: the one thing on this
            // pedal that is a choice from a short list.
            const auto pc = event.AsProgramChange();
            if(pc.program < PedalState::kPageCount)
            {
                state_->SetPage(pc.program);
                accepted_++;
            }
            break;
        }

        default: break;
    }
}

void MidiControl::ApplyNote(uint8_t note, uint8_t velocity, bool on)
{
    accepted_++;

    if(on)
        state_->NoteOn(note, velocity); // velocity 0 is handled in the model
    else
        state_->NoteOff(note);
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

        case midimap::Target::PAGE:
            state_->SetPage(midimap::Quantise(value, PedalState::kPageCount));
            break;

        case midimap::Target::GATE:
            // A separate flag from the footswitch's and from the keys': the
            // model ORs them, so none of the three can cancel another.
            state_->SetGateRemote(midimap::IsOn(value));
            break;

        case midimap::Target::BYPASS:
            state_->SetBypass(!midimap::IsOn(value));
            break;

        default: break;
    }
}
