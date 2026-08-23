#pragma once
#ifndef BOONTA_CHAIN_H
#define BOONTA_CHAIN_H

#include <stddef.h>

#include "DriveEffect.h"
#include "EqEffect.h"
#include "PedalState.h"
#include "ReverbEffect.h"

/** DSP.
 *
 *  Owns the three effects and runs them in whatever order the model says,
 *  wrapped in the performance page's input trim, per-slot amounts, output level
 *  and global dry/wet. It has no idea a knob exists: it asks the model for
 *  Drive(DRIVE_GAIN), so the same chain works when driven by expression, by
 *  MIDI or by a unit test.
 *
 *  Buffers are raw pointers rather than AudioHandle::InputBuffer so this file
 *  stays independent of libDaisy.
 */
class Chain
{
  public:
    /** Longest block the internal scratch buffers can hold. Anything larger is
     *  processed in several passes rather than truncated, so the chain is
     *  correct at any block size the audio engine is configured for. */
    static constexpr size_t kMaxBlockSize = 128;

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
    /** One pass over at most kMaxBlockSize frames. */
    void ProcessChunk(const PedalState& state,
                      const float*      in_l,
                      const float*      in_r,
                      float*            out_l,
                      float*            out_r,
                      size_t            size);

    void RunSlot(PedalState::Slot  slot,
                 const PedalState& state,
                 float*            left,
                 float*            right,
                 size_t            size);

    EqEffect     eq_;
    DriveEffect  drive_;
    ReverbEffect reverb_;

    // Scratch. dry_ is the chain input, kept for the global mix; slot_ is one
    // effect's input, kept for that slot's amount crossfade.
    float dry_l_[kMaxBlockSize], dry_r_[kMaxBlockSize];
    float slot_l_[kMaxBlockSize], slot_r_[kMaxBlockSize];
    float work_l_[kMaxBlockSize], work_r_[kMaxBlockSize];
};

#endif
