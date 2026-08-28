#pragma once
#ifndef BOONTA_EQ_EFFECT_H
#define BOONTA_EQ_EFFECT_H

#include <stddef.h>

#include "Biquad.h"
#include "Effect.h"

/** Three band semi-parametric EQ: sweepable low shelf, peaking mid, high shelf.
 *
 *  Six knobs buy three corner frequencies and three gains; band width is the
 *  one thing left over, so it lives on TOG_SW_3 and moves all three bands
 *  together.
 *
 *  Processes in place. Coefficients are designed at block rate, and only when a
 *  knob has actually moved -- three RBJ designs is a dozen transcendentals, and
 *  paying for them on every block while nobody is touching the pedal would be
 *  the largest single cost in this effect.
 */
class EqEffect : public Effect
{
  public:
    EqEffect() : sample_rate_(48000.f), coeffs_valid_(false) {}

    void Init(float sample_rate) override;

    void Process(const float* params, int toggle,
                 float* left, float* right, size_t size) override;

    const EffectDesc& Desc() const override;

  private:
    /** Redesign the three sections if, and only if, something changed. */
    void UpdateCoeffs(const float* params, int toggle);

    float sample_rate_;

    BiquadCoeffs low_, mid_, high_;
    BiquadState  low_z_[2], mid_z_[2], high_z_[2];

    // Cache of the settings the current coefficients were designed from.
    float coeffs_from_[Effect::kParamCount];
    int   coeffs_from_q_ = 0;
    bool  coeffs_valid_;
};

#endif
