#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include <stdint.h>

#include "PedalState.h"

/** DSP: a granular processor in the shape of Mutable Instruments' Beads.
 *
 *  Reads the model, writes samples. It has no idea a knob exists: it asks the
 *  model for PARAM_TIME, so the same effect works when driven by a knob, by
 *  expression or by a unit test. Buffers are raw pointers rather than
 *  AudioHandle::InputBuffer so this file stays independent of libDaisy.
 *
 *  ## How it works
 *
 *  Everything the pedal plays comes out of one circular recording buffer in
 *  SDRAM, about twenty-two seconds long, holding 16-bit samples. Audio is
 *  written into it continuously, *including while the pedal is bypassed*, so
 *  that engaging it has twenty-two seconds of material to work with rather
 *  than starting from silence. Freeze is simply the write head standing
 *  still: the grains have no idea it happened, they just keep reading a
 *  window that has stopped being overwritten.
 *
 *  ## What makes it Beads rather than Clouds
 *
 *  Same buffer, same grain pool, three different decisions:
 *
 *  - **The grain envelope is asymmetric.** SHAPE sets how much of a grain is
 *    spent rising: at the bottom a grain is a pluck, in the middle a
 *    triangle, at the top a backwards swell. One knob covers percussive
 *    through to reversed-tape.
 *  - **Every grain gets its own pitch.** The SEED toggle scatters pitch, pan
 *    and start point per grain, and the select buttons quantise the result,
 *    so a scattered cloud can be made to land on notes.
 *  - **The recording path has a character.** The QUALITY toggle saturates it
 *    and, at the top, reduces it to eight bits at a quarter of the sample
 *    rate -- in the buffer, the way Beads does it, so what comes back out is
 *    what was recorded rather than a filter on the way past.
 *
 *  ## Cost
 *
 *  Each live grain costs two interpolated SDRAM reads a sample. kMaxGrains is
 *  the ceiling on how many run at once and is the first number to lower if a
 *  dense cloud at a high sample rate runs out of time in the callback.
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
     *  \param in_peak Out: peak of the signal as recorded, for the clip LED.
     *  \return       Peak output magnitude over the block, for the meter.
     */
    float Process(const PedalState&   state,
                  const float* const* in,
                  float**             out,
                  size_t              size,
                  float*              in_peak);

  private:
    /** Grains running at once. Twenty-four is enough that a dense cloud of
     *  long grains never audibly thins out, and few enough to leave headroom
     *  in the callback. */
    static constexpr int kMaxGrains = 24;

    /** One window into the recording buffer.
     *
     *  The read position is split into an integer index and a fraction rather
     *  than held in one float: at the far end of a million-sample buffer a
     *  float has well under a sample of resolution left, which is audible as
     *  pitch drift on long grains. */
    struct Grain
    {
        bool     active;
        uint32_t ipos;    /**< integer read index, masked to the buffer */
        float    frac;    /**< fractional part of the read position */
        float    inc;     /**< read increment: this grain's own pitch ratio */
        uint32_t env_pos; /**< samples played so far */
        uint32_t env_len; /**< total length in samples */
        float    env_rcp; /**< 1 / env_len, so the window is a multiply */
        float    attack;  /**< fraction of the grain spent rising */
        float    pan_l;
        float    pan_r;
    };

    void UpdateParams(const PedalState& state);
    void Schedule();
    void Spawn();
    void RenderGrains(float& out_l, float& out_r);

    /** This grain's pitch ratio: the PITCH knob, plus whatever the SEED
     *  toggle scatters on top, snapped by the current quantisation. */
    float GrainPitchRatio();

    uint32_t Rng();
    float    RandF();  /**< [0, 1) */
    float    RandBi(); /**< [-1, 1) */

    float sample_rate_;

    Grain    grain_[kMaxGrains];
    uint32_t write_       = 0;    /**< buffer write head */
    float    spawn_timer_ = 0.f;  /**< samples until the next grain */
    uint32_t rng_         = 0x2545f491u;

    // --- block-rate parameters, resolved in UpdateParams() -------------
    bool                 frozen_      = false;
    PedalState::Quantise quantise_    = PedalState::QUANT_FREE;
    float                lag_         = 4096.f; /**< TIME, in samples */
    uint32_t             grain_len_   = 2048;
    float                base_semis_  = 0.f;
    float                seed_semis_  = 0.f;
    float                seed_pan_    = 0.f;
    float                scatter_     = 0.f; /**< start jitter, in samples */
    float                interval_    = 1024.f;
    float                randomness_  = 0.f;
    float                shape_       = 0.5f;
    float                feedback_    = 0.f;
    float                mix_         = 0.f;
    float                wet_gain_    = 1.f;
    float                drive_       = 1.f;
    bool                 lofi_        = false;

    float fb_damp_l_ = 0.f;
    float fb_damp_r_ = 0.f;
    float fb_damp_coeff_ = 1.f;

    /** Sample-and-hold state for the lo-fi recording path. */
    float    deci_hold_l_ = 0.f;
    float    deci_hold_r_ = 0.f;
    uint32_t deci_count_  = 0;
};

#endif
