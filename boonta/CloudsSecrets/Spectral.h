#pragma once
#ifndef BOONTA_SPECTRAL_H
#define BOONTA_SPECTRAL_H

#include <stddef.h>
#include <stdint.h>

/** Spectral madness: the hidden fourth mode.
 *
 *  A phase vocoder that is allowed to misbehave. The input is cut into
 *  overlapping 1024-sample frames, each frame is windowed and Fourier
 *  transformed, and what is kept of it is, per bin, a *magnitude* and an
 *  *instantaneous frequency* (the bin's own frequency corrected by how far its
 *  phase advanced since the last frame). Frames are stored in a small set of
 *  snapshot slots. Output is made by taking a frame, mangling it, running a
 *  phase accumulator at each bin's frequency to build fresh phases, inverse
 *  transforming and overlap-adding.
 *
 *  Keeping frequency rather than phase is what makes a stored frame playable:
 *  re-synthesised over and over, a frozen frame keeps ringing as a sustained
 *  tone instead of buzzing at the frame rate.
 *
 *  ## The knobs, in this mode
 *
 *  POSITION  Which snapshot slot. Unfrozen it chooses the slot that is being
 *            written; frozen it scans smoothly between the stored slots.
 *  SIZE      Remaps frequencies on the way out. Left of centre the whole
 *            spectrum slides upward; right of centre it is progressively
 *            mixed with its own mirror image, ending in full reversal.
 *  PITCH     Resamples the bins: a spectral transposition, not a time one.
 *  DENSITY   Left of centre, a growing share of bins stop updating (partial
 *            freeze). Right of centre, successive frames are averaged, which
 *            is a low-pass on every bin's level, and past 90% random phase
 *            is added until the sound dissolves into a wash.
 *  TEXTURE   Left of centre, bin levels are quantised, which sounds like a
 *            low bit-rate codec. Right of centre the spectrum is flattened:
 *            the loudest partials come down and the quietest come up.
 *
 *  Every knob is neutral at centre.
 *
 *  ## Cost, and why it is a state machine
 *
 *  One frame costs two 1024-point FFTs and a few thousand bin updates, a bit
 *  over a block's worth of CPU if done in one go. Since the callback must
 *  finish each block in a block's time, the work for one frame is cut into
 *  five stages and Service() runs one of them per audio block. The input
 *  capture and the overlap-add positions are computed from sample counts, so
 *  doing the work late changes how long the output is delayed, never where in
 *  time a frame lands.
 *
 *  Both channels share one complex FFT, left as the real part and right as
 *  the imaginary part.
 *
 *  Knows nothing about libDaisy; builds and tests on a host.
 */
class Spectral
{
  public:
    static constexpr int kFftSize = 1024;
    static constexpr int kHop     = 256; /**< four-fold overlap */
    static constexpr int kBins    = kFftSize / 2 + 1;
    static constexpr int kSlots   = 6;

    /** Samples between a sample going in and its processed version coming
     *  out. A frame's last covering window ends kFftSize samples after the
     *  sample it finishes, and the work for it is spread over a few blocks. */
    static constexpr int kLatency = kFftSize + 2 * kHop;

    struct Params
    {
        bool  frozen   = false;
        float position = 0.f; /**< 0..1 */
        float size     = 0.5f;
        float ratio    = 1.f; /**< pitch ratio */
        float density  = 0.5f;
        float texture  = 0.5f;
    };

    Spectral() {}

    void Init();

    /** Block-rate. Cheap. */
    void SetParams(const Params& p) { params_ = p; }

    /** One stereo frame in, one out. Per-sample, trivial cost. */
    void Tick(float in_l, float in_r, float& out_l, float& out_r);

    /** Call once per block, after the block's Tick()s. Runs as much of the
     *  pending frame as fits in a block of this many samples. */
    void Service(size_t block);

  private:
    enum Stage
    {
        STAGE_IDLE = -1,
        STAGE_FFT  = 0, /**< capture and forward transform */
        STAGE_ANALYSE,  /**< bins to magnitude and frequency, store a slot */
        STAGE_MANGLE,   /**< choose a frame, warp, transpose, density */
        STAGE_SYNTH,    /**< texture, phase accumulation, pack the spectrum */
        STAGE_IFFT,     /**< inverse transform and overlap-add */
        STAGE_LAST,
    };

    void RunStage();

    void StageFft();
    void StageAnalyse();
    void StageMangle();
    void StageSynth();
    void StageIfft();

    uint32_t Rng();
    float    RandF(); /**< [0, 1) */

    Params params_;

    uint32_t write_count_ = 0; /**< samples ever pushed in */
    uint32_t next_frame_  = 0; /**< sample count the next frame ends at */
    uint32_t frame_end_   = 0; /**< the frame being worked on */
    int      stage_       = STAGE_IDLE;
    bool     have_prev_   = false;
    bool     lock_        = false; /**< this frame can use its own phases */
    Params   job_;             /**< params snapshot for the frame in flight */

    uint32_t rng_ = 0x2545F491u;
};

#endif
