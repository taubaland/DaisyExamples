#pragma once
#ifndef BOONTA_REVERB_EFFECT_H
#define BOONTA_REVERB_EFFECT_H

#include <stddef.h>

#include "PedalState.h"

/** Dattorro plate reverb.
 *
 *  Written out rather than taken from DaisySP for two reasons: ReverbSc lives
 *  in the DaisySP-LGPL submodule, which this checkout does not have and which
 *  would put an LGPL component in a pedal binary; and it exposes only feedback
 *  and damping, whereas the reverb page promises pre-delay and diffusion as
 *  well. Both fall out of a plate topology for free.
 *
 *  Signal path: pre-delay, low cut, bandwidth limit, four input diffusers, then
 *  a figure-of-eight tank of two halves, each an allpass into a delay into a
 *  damping lowpass into a second allpass into a second delay, cross-fed. Two of
 *  the allpasses are slowly modulated, which is what stops a plate ringing on a
 *  fixed set of modes. Stereo output comes from a fixed set of taps into those
 *  lines rather than from running two reverbs, which is also how a real plate
 *  works: one plate, two pickups.
 *
 *  The delay memory is a file-static block in SDRAM -- see the .cpp -- so the
 *  object itself stays small and nothing lands in the 32K DMA region that the
 *  bootloader linker scripts leave us.
 *
 *  Processes in place.
 */
class ReverbEffect
{
  public:
    ReverbEffect() : sample_rate_(48000.f) {}

    void Init(float sample_rate);

    void Process(const PedalState& state, float* left, float* right, size_t size);

  private:
    /** Resolve the model into delay lengths and coefficients, once per block. */
    void UpdateCoeffs(const PedalState& state, size_t size);

    float sample_rate_;

    /** Samples per unit of Dattorro's original 29761 Hz design, times the size
     *  multiplier from TOG_SW_2. Every length and tap in the tank is a constant
     *  from the paper multiplied by this. */
    float unit_ = 1.f;

    // Block-rate coefficients.
    float predelay_    = 0.f;
    float decay_       = 0.5f;
    float damp_coeff_  = 1.f;
    float lowcut_coeff_ = 0.01f;
    float in_diff1_    = 0.75f;
    float in_diff2_    = 0.625f;
    float dec_diff1_   = 0.7f;
    float dec_diff2_   = 0.5f;
    float mix_         = 0.3f;
    float size_trim_   = 1.f;
    float mod_l_       = 0.f;
    float mod_r_       = 0.f;

    // Filter state.
    float lowcut_z_ = 0.f;
    float band_z_   = 0.f;
    float damp_l_z_ = 0.f;
    float damp_r_z_ = 0.f;

    // Tank modulation LFOs, advanced once per block.
    float lfo_l_phase_ = 0.f;
    float lfo_r_phase_ = 0.25f;

    float bandwidth_coeff_ = 1.f;
};

#endif
