#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include <stdint.h>

#include "PedalState.h"
#include "Reverb.h"

/** DSP: a granular processor in the shape of Mutable Instruments' Clouds.
 *
 *  Reads the model, writes samples. It has no idea a knob exists: it asks the
 *  model for PARAM_POSITION, so the same effect works when driven by a knob,
 *  by expression or by a unit test. Buffers are raw pointers rather than
 *  AudioHandle::InputBuffer so this file stays independent of libDaisy.
 *
 *  ## How it works
 *
 *  Everything the pedal plays comes out of one circular recording buffer in
 *  SDRAM, about eleven seconds long, holding 16-bit samples -- the same
 *  compromise Clouds makes, and for the same reason: the grain reads are
 *  scattered, so halving the bytes per read is worth more than the last two
 *  bits of resolution.
 *
 *  Audio is written into that buffer continuously, *including while the pedal
 *  is bypassed*, so that engaging it has eleven seconds of material to work
 *  with rather than starting from silence. Freeze is simply the write head
 *  standing still: the grains have no idea it happened, they just keep
 *  reading a window that has stopped being overwritten.
 *
 *  A grain is a short window into that buffer, read at its own rate and faded
 *  in and out so the ends do not click. A pool of them is kept, a scheduler
 *  starts new ones, and the sum of whatever is playing is the output. The
 *  three playback modes differ only in how the scheduler picks start points:
 *
 *    MODE_GRANULAR  random start points, random spacing, random panning
 *    MODE_STRETCH   one playhead moving at its own rate, grains overlapped
 *                   four deep over it -- time stretching
 *    MODE_DELAY     the same playhead locked to real time, two grains deep,
 *                   with feedback -- a delay you can transpose and freeze
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
     *  \return       Peak output magnitude over the block, for the meter.
     */
    float Process(const PedalState&   state,
                  const float* const* in,
                  float**             out,
                  size_t              size);

  private:
    /** Grains running at once. Twenty-four is enough that a dense cloud of
     *  long grains never audibly thins out, and few enough to leave headroom
     *  in the callback. */
    static constexpr int kMaxGrains = 24;

    /** One window into the recording buffer.
     *
     *  The read position is split into an integer index and a fraction rather
     *  than held in one float: at the far end of a half-million sample buffer
     *  a float has about a fifth of a sample of resolution left, which is
     *  audible as pitch drift on long grains. */
    struct Grain
    {
        bool     active;
        uint32_t ipos;    /**< integer read index, masked to the buffer */
        float    frac;    /**< fractional part of the read position */
        float    inc;     /**< read increment: the pitch ratio */
        uint32_t env_pos; /**< samples played so far */
        uint32_t env_len; /**< total length in samples */
        float    env_rcp; /**< 1 / env_len, so the window is a multiply */
        float    ramp;    /**< window fade length, as a fraction of env_len */
        float    pan_l;
        float    pan_r;
    };

    void  UpdateParams(const PedalState& state);
    void  Schedule();
    void  Spawn(uint32_t start_lag);
    void  RenderGrains(float& out_l, float& out_r);

    /** Distance behind the write head that a fresh grain should start at,
     *  given the current POSITION setting. */
    uint32_t AnchorLag() const;

    uint32_t Rng();
    float    RandF();  /**< [0, 1) */
    float    RandBi(); /**< [-1, 1) */

    float sample_rate_;

    Grain    grain_[kMaxGrains];
    uint32_t write_ = 0;    /**< buffer write head */
    float    spawn_timer_ = 0.f; /**< samples until the next grain */
    int      next_grain_  = 0;   /**< round-robin slot, for the fixed modes */
    int      voices_      = 2;   /**< slots the current mode uses */
    uint32_t rng_         = 0x12345678u;

    /** Playhead for MODE_STRETCH and MODE_DELAY, held as a distance behind
     *  the write head so that freezing the write head does not move it. */
    float lag_ = 0.f;

    // --- block-rate parameters, resolved in UpdateParams() -------------
    PedalState::Mode mode_      = PedalState::MODE_GRANULAR;
    bool             frozen_    = false;
    float            position_  = 0.f;
    uint32_t         grain_len_ = 2048;
    float            pitch_inc_ = 1.f;
    float            interval_  = 1024.f; /**< samples between grains */
    float            randomness_ = 0.f;
    float            scatter_   = 0.f; /**< start-point jitter, in samples */
    float            ramp_      = 0.25f;
    float            spread_    = 0.f;
    float            feedback_  = 0.f;
    float            reverb_    = 0.f;
    float            mix_       = 0.f;
    float            wet_gain_  = 1.f;
    float            play_rate_ = 1.f;
    float            fb_damp_coeff_ = 1.f;

    float fb_damp_l_ = 0.f;
    float fb_damp_r_ = 0.f;

    Reverb reverb_tank_;
};

#endif
