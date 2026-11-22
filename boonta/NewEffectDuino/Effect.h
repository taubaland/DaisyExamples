#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include "PedalState.h"

/** DSP.
 *
 *  This is the file to write. Everything else in the project exists to hand
 *  you a block of samples with the controls already read, debounced and
 *  smoothed into the model, and the LEDs and relays already taken care of.
 *
 *  It reads the model and writes samples. It has no idea a knob exists: it
 *  asks the model for PARAM_DRIVE, so the same effect works whether that value
 *  came from a knob, the expression pedal or a test harness. Buffers are raw
 *  pointers rather than AudioHandle::InputBuffer so this file stays
 *  independent of libDaisy and still compiles on a host.
 *
 *  As it stands it passes audio through unchanged. To build an effect: add
 *  state below, resolve the model into coefficients once per block at the top
 *  of Process(), and do the per-sample work in ProcessChannel().
 */
class Effect
{
  public:
    Effect() : sample_rate_(48000.f) {}

    /** Clear state and work out anything that depends on the sample rate.
     *  \param sample_rate  Audio rate in Hz. */
    void Init(float sample_rate);

    /** Process one block. Audio-callback context: no allocation, no blocking.
     *  \param state  Current model state, sampled once per block.
     *  \param in     Non-interleaved input channels.
     *  \param out    Non-interleaved output channels.
     *  \param size   Frames per channel.
     */
    void Process(const PedalState&   state,
                 const float* const* in,
                 float**             out,
                 size_t              size);

  private:
    /** One sample of one channel.
     *  \param channel  0 = left, 1 = right. Index per-channel state with it. */
    float ProcessChannel(int channel, float in);

    float sample_rate_;

    // Per-channel filter state goes here, two of each -- one per channel --
    // indexed by the `channel` argument to ProcessChannel(). Clear it in
    // Init(), not in the constructor, so a sample-rate change resets it too.

    // Block-rate coefficients go here, resolved from the model in Process()
    // and read by ProcessChannel().
};

#endif
