#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include "PedalState.h"

/** DSP.
 *
 *  Reads the model, writes samples. It has no idea a knob exists: it asks the
 *  model for PARAM_DRIVE, so the same effect works when driven by expression,
 *  by MIDI or by a unit test. Buffers are raw pointers rather than
 *  AudioHandle::InputBuffer so this file stays independent of libDaisy.
 *
 *  This is the file to replace when you build a real pedal. The stock effect
 *  is a two-channel drive -> tone -> level chain, preset-switched between
 *  three gain characters, with the alt footswitch as a gain boost.
 */
class Effect
{
  public:
    Effect() : sample_rate_(48000.f) {}

    void Init(float sample_rate);

    /** Process one block.
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
    float ProcessChannel(int channel, float in);

    float sample_rate_;

    // Per-channel filter state.
    float tone_z_[2] = {0.f, 0.f};
    float dc_z_[2]   = {0.f, 0.f};

    // Block-rate coefficients, recomputed in Process().
    float drive_       = 1.f;
    float tone_coeff_  = 1.f;
    float level_       = 1.f;
    float mix_         = 1.f;
};

#endif
