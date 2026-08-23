#pragma once
#ifndef BOONTA_DRIVE_EFFECT_H
#define BOONTA_DRIVE_EFFECT_H

#include <stddef.h>

#include "PedalState.h"

/** Gain stage: bias -> waveshaper -> DC block -> tone -> level -> mix.
 *
 *  CHARACTER morphs continuously across three shapers rather than switching
 *  between them, so the knob sweeps from a soft cubic clip, through a hard
 *  clip, into a wavefolder without a step anywhere. BIAS offsets the signal
 *  into the shaper, which is what puts even harmonics in; the DC it leaves
 *  behind is removed after the shaper, not before, or the offset would have
 *  nothing to act on.
 *
 *  Processes in place.
 */
class DriveEffect
{
  public:
    DriveEffect() : sample_rate_(48000.f) {}

    void Init(float sample_rate);

    void Process(const PedalState& state, float* left, float* right, size_t size);

  private:
    float ProcessSample(int channel, float in);

    float sample_rate_;

    // Per-channel filter state.
    float tone_z_[2] = {0.f, 0.f};
    float dc_z_[2]   = {0.f, 0.f};

    // Block-rate coefficients, resolved in Process().
    float gain_       = 1.f;
    float bias_       = 0.f;
    float character_  = 0.f;
    float tone_coeff_ = 1.f;
    float dc_coeff_   = 0.001f;
    float level_      = 1.f;
    float mix_        = 1.f;
};

#endif
