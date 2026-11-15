#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include <stdint.h>

#include "PedalState.h"
#include "PitchTracker.h"

namespace elements
{
class Part;
}

/** DSP: Mutable Instruments' Elements, with the input jack as an exciter.
 *
 *  Reads the model, writes samples. The synthesis itself is Emilie Gillet's
 *  code, vendored unmodified under `elements/`; what is here is the part of
 *  the module that was hardware -- the job `cv_scaler.cc` and `elements.cc`
 *  did for a panel of pots and CV jacks, done instead for a model that a
 *  pedal's knobs, a footswitch and a MIDI cable all write.
 *
 *  It knows nothing about libDaisy, so it builds and runs on a host.
 *
 *  ## Signal flow
 *
 *      input jack -> gain -> noise gate -+-> strike in  (straight to the
 *                                        |               resonator)
 *                                        +-> blow in    (through the
 *                                        |               diffuser first)
 *                                        +-> envelope follower -> gate
 *                                        +-> pitch tracker -> note
 *                                            (trigger toggle at centre)
 *
 *      gate, note, strength -> bow / blow / strike exciters -> resonator
 *                           -> stereo spread -> reverb -> output
 *
 *  ## Rate
 *
 *  Elements is written for 32 kHz and its lookup tables are computed for it,
 *  so the codec is run at 32 kHz rather than the tables being regenerated.
 *  Process() takes any block size and feeds the Part in chunks of at most
 *  sixteen samples, which is the largest it accepts.
 */
class Effect
{
  public:
    static constexpr float kSampleRate = 32000.f;

    Effect() : part_(nullptr) {}

    /** \param part           zero-filled storage for the Part. Zeroed
     *                        matters: upstream keeps it in .bss and several
     *                        of its members are never otherwise initialised.
     *  \param reverb_buffer  32768 words for the reverb's delay memory
     *  \param seed, seed_len a per-unit number; Elements derives small
     *                        per-unit differences in the reverb and the
     *                        modulation from it, as the module does from its
     *                        serial number */
    void Init(elements::Part* part,
              uint16_t*       reverb_buffer,
              uint32_t*       seed,
              size_t          seed_len);

    /** Render one block. Audio-callback context.
     *  \return the block's output peak, for the meter. */
    float Process(PedalState&         state,
                  const float* const* in,
                  float**             out,
                  size_t              size);

  private:
    /** One chunk of at most kChunk samples. */
    float ProcessChunk(const PedalState& state,
                       const float*      in_l,
                       const float*      in_r,
                       float*            out_l,
                       float*            out_r,
                       size_t            size);

    void UpdatePatch(const PedalState& state);
    bool InputGate(float chunk_peak);

    static constexpr size_t kChunk = 16;

    elements::Part* part_;

    /** One-pole smoothed copy of every parameter, stepped once a chunk.
     *  Knobs arrive stepped at the block rate and MIDI arrives in 127ths;
     *  the resonator turns either into zipper noise. */
    float smoothed_[PedalState::PARAM_LAST];

    /** Coarse pitch, in whole semitones, with hysteresis so a knob resting
     *  on a boundary does not trill. */
    int pitch_semitones_;

    // Input conditioning.
    float input_level_; /**< noise gate detector */

    // Input-driven gate.
    float env_;         /**< fast peak follower */
    float env_floor_;   /**< what the note in progress has decayed to */
    bool  input_gate_;
    int   holdoff_;     /**< chunks until another attack may retrigger */
    int   retrigger_;   /**< chunks left of forced gate-low */

    /** Follows the guitar's pitch. Fed whenever the pedal is in circuit, so
     *  it already knows the note when the trigger toggle selects it. */
    PitchTracker tracker_;
    uint32_t     tracker_generation_;
};

#endif
