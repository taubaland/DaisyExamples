#pragma once
#ifndef BOONTA_MIDI_CONTROL_H
#define BOONTA_MIDI_CONTROL_H

#include "daisy_boonta.h"
#include "hid/usb_midi.h"

#include "MidiMap.h"
#include "PedalState.h"
#include "SavedState.h"
#include "Sysex.h"

/** Controller, remote.
 *
 *  The same job as Controls -- turn input into intent and write it into the
 *  model -- for a controller at the other end of a cable instead of a knob. It
 *  drives nothing else and reads no samples.
 *
 *  Listens on USB and the UART together and treats them as one stream, so it
 *  does not matter which one a message arrives on. The board constructs a
 *  MidiUartHandler in its Init() but never starts it; USB is not brought up at
 *  all. Both are started here.
 *
 *  Runs in the audio callback, alongside Controls, so that everything writing
 *  the model does so from one context. Parsing is a handful of bytes per event.
 *
 *  Preset recall is the exception: a program change only *requests* a preset,
 *  because loading one means touching flash and that belongs in the main loop.
 *  Storage picks the request up there.
 */
class MidiControl
{
  public:
    MidiControl() : hw_(nullptr), state_(nullptr) {}

    void Init(daisy::DaisyBoonta* hw, PedalState* state);

    /** Drain both ports and apply whatever arrived. Audio-callback context. */
    void Process();

    /** Program change since the last call, or -1. Read from the main loop. */
    int TakePresetRequest();

    /** Answer any pending SysEx request. Main-loop context, and it has to be:
     *  a catalogue is a few hundred bytes per effect, and the transports send
     *  it by blocking. Doing that from the audio callback would drop buffers
     *  for as long as the reply took.
     *
     *  Sends at most one message per call, so a growing registry lengthens the
     *  reply rather than lengthening any single pass of the main loop. Cheap
     *  when there is nothing to send, which is almost always. */
    void ServiceSysex();

    /** SysEx messages sent since boot. Non-zero means an editor asked and got
     *  an answer, which is the one thing a silent link cannot fake. */
    uint32_t SysexSent() const { return sysex_sent_; }

    /** Messages that arrived at all, per transport, and those that actually
     *  matched something in the map.
     *
     *  Kept separate because they answer different questions. If received is
     *  zero the link is dead; if received climbs but accepted does not, the
     *  link is fine and the controller is simply sending numbers we do not
     *  claim. Reading only the second cannot tell those apart, which cost a
     *  debugging round trip. */
    uint32_t Accepted() const { return accepted_; }
    uint32_t ReceivedUart() const { return received_uart_; }
    uint32_t ReceivedUsb() const { return received_usb_; }

  private:
    void Handle(daisy::MidiEvent event);
    void ApplyControlChange(uint8_t cc, uint8_t value);
    void HandleSysex(const daisy::MidiEvent& event);

    daisy::DaisyBoonta*   hw_;
    PedalState*           state_;
    daisy::MidiUsbHandler usb_;

    volatile int preset_request_ = -1;

    /** What the main loop still owes an editor.
     *
     *  A queue of one identity reply and a cursor through the catalogue,
     *  rather than a general outbox: those are the only two things that can be
     *  asked for, and a fixed pair of flags cannot overflow or need draining
     *  under an interrupt. `catalogue_next_` is the id of the next effect to
     *  describe, or -1 when there is nothing to send. */
    volatile bool identity_pending_ = false;
    volatile int  catalogue_next_   = -1;

    volatile uint32_t sysex_sent_    = 0;
    volatile uint32_t accepted_      = 0;
    volatile uint32_t received_uart_ = 0;
    volatile uint32_t received_usb_  = 0;
};

#endif
