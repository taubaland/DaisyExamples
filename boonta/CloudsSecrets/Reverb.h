#pragma once
#ifndef BOONTA_REVERB_H
#define BOONTA_REVERB_H

/** A small plate, for the tail at the end of the grain chain.
 *
 *  Clouds puts a reverb after the granulator and hides it under the BLEND
 *  knob; without one, a sparse cloud is a row of disconnected events rather
 *  than a texture. This is the cheapest thing that does that job: a four
 *  allpass diffuser into two damped delay loops that feed each other, which
 *  is Dattorro's topology with the modulation and the multi-tap output left
 *  out.
 *
 *  Its delay memory lives in SDRAM and is declared at file scope in the .cpp,
 *  so there is exactly one Reverb in the program.
 */
class Reverb
{
  public:
    Reverb() : sample_rate_(48000.f), decay_(0.6f), damp_coeff_(0.4f) {}

    void Init(float sample_rate);

    /** \param decay    0..1, loop gain. Above about 0.85 it rings forever.
     *  \param damp_hz  Lowpass in the loop, in Hz. */
    void SetTail(float decay, float damp_hz);

    /** One stereo frame in, the wet tail out. The caller does the mixing. */
    void Process(float in_l, float in_r, float& out_l, float& out_r);

  private:
    float sample_rate_;
    float decay_;
    float damp_coeff_;

    float damp_z_l_    = 0.f;
    float damp_z_r_    = 0.f;
    float bandwidth_z_ = 0.f;

    /** Carried across frames: each loop is fed by the other one. */
    float cross_l_ = 0.f;
    float cross_r_ = 0.f;
};

#endif
