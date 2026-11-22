#pragma once
#ifndef BOONTA_RNBO_EFFECT_H
#define BOONTA_RNBO_EFFECT_H

/** An exported RNBO patch in a slot.
 *
 *  This is an implementation header, not an interface: it must be included
 *  *after* RnboPatch.h, which brings the RNBO library into scope, and only from
 *  a per-patch translation unit. The rest of the firmware asks for these
 *  effects through RnboEffects.h and never sees any of RNBO.
 *
 *  ## Build time, not runtime
 *
 *  RNBO's C++ export is a code generator: the artefact is source you compile
 *  in, so a patch is added by exporting it, pointing the Makefile at it and
 *  rebuilding. There is nothing to parse on the pedal.
 *
 *      make RNBO_DIR=/path/to/exports
 *
 *  where that directory holds one subdirectory per patch. See the Makefile.
 *
 *  ## Three flags the export needs, and why
 *
 *  - `RNBO_NOTHROW` -- the default platform throws `std::out_of_range` on
 *    error, and the firmware builds `-fno-exceptions`. RNBO already has this
 *    switch; it prints instead.
 *  - `RNBO_USE_FLOAT32` -- otherwise `SampleValue` is `double`, which on this
 *    part means slower arithmetic *and* a conversion pass in and out of every
 *    block, because the chain's buffers are float. With it, RNBO reads and
 *    writes our buffers directly. Note this governs the signal path only: a
 *    data ref is a Float64Buffer either way, which is why the delay lines cost
 *    what they do.
 *  - `RNBO_USECUSTOMALLOCATOR` -- the patches allocate megabytes of buffers and
 *    the heap is 256 KB. See RnboHeap.cpp.
 *
 *  `RNBO_NOSTL` is *not* set. The export uses the standard library in places,
 *  and turning it off is a larger fight than it is worth.
 *
 *  ## Six knobs and a toggle
 *
 *  That is the whole control surface an Effect gets, and these patches declare
 *  four, seven and twelve parameters. Everything past the seventh control is
 *  left at the value the patch was exported with -- which is a choice about
 *  what is worth reaching for, and is spelled out in each adapter. A knob that
 *  does nothing is honest; one wired to something arbitrary is not.
 */

#include "Chain.h" // kMaxBlockSize -- the largest block a patch is prepared for
#include "Effect.h"
#include "RnboHeap.h"

/** A knob this patch does not use. */
constexpr int kRnboUnmapped = -1;

/** One knob, and the patch parameter it drives. */
struct RnboParam
{
    int   index;  /**< RNBO parameter index, or kRnboUnmapped */
    float lo, hi; /**< the range that parameter declares */
};

/** A toggle that sets a parameter of its own: one value per position. */
struct RnboToggleParam
{
    int   index; /**< kRnboUnmapped if the toggle drives no parameter */
    float value[3];
};

/** A toggle that changes what a knob *means*, by choosing how far it reaches.
 *  A parameter that spends its life near the centre of a wide range cannot be
 *  set by hand across the whole of it, so the toggle picks the span and the
 *  knob sweeps +/- that from its centre detent. */
struct RnboToggleRange
{
    int   knob; /**< kRnboUnmapped if the toggle scales no knob */
    float span[3];
};

/** Everything that makes one patch different from another. */
struct RnboPatchSpec
{
    const EffectDesc* desc;
    const RnboParam*  map; /**< Effect::kParamCount entries */
    RnboToggleParam   toggle_param;
    RnboToggleRange   toggle_range;
};

template <class Patcher>
class RnboEffect : public Effect
{
  public:
    explicit RnboEffect(const RnboPatchSpec& spec) : spec_(spec) {}

    void Init(float sample_rate) override
    {
        // initialize() builds the patch's objects and prepareToProcess() sizes
        // its buffers; both allocate. This is the only place either is called,
        // and it runs from main() before the audio callback starts.
        patcher_ = rnbo_heap::NewPatcher<Patcher>();

        if(!patcher_)
        {
            // Out of pool. Process() will pass audio through untouched, which
            // is the only safe thing left -- see RnboHeap.cpp on why running
            // anyway is not an option.
            ready_ = false;
            return;
        }

        patcher_->initialize();
        patcher_->prepareToProcess(sample_rate, Chain::kMaxBlockSize, true);

        // The patch comes up with the values it was exported with, not with
        // what the knobs say, so nothing may be skipped on the first block.
        primed_      = false;
        last_toggle_ = -1;
        ready_       = true;
    }

    void Process(const float* params,
                 int          toggle,
                 float*       left,
                 float*       right,
                 size_t       size) override
    {
        if(!ready_)
            return;

        if(toggle < 0)
            toggle = 0;
        if(toggle > 2)
            toggle = 2;

        const bool toggle_moved = (toggle != last_toggle_);
        last_toggle_            = toggle;

        if(spec_.toggle_param.index != kRnboUnmapped && toggle_moved)
            Send(spec_.toggle_param.index, spec_.toggle_param.value[toggle]);

        for(int i = 0; i < Effect::kParamCount; i++)
        {
            const RnboParam& m = spec_.map[i];

            if(m.index == kRnboUnmapped)
                continue;

            // A scaled knob means something different once the toggle has
            // moved, so it has to be re-sent even when the knob has not.
            const bool scaled = (spec_.toggle_range.knob == i);

            // Only what moved. setParameterValue is not free -- it costs about
            // 2.8 points of CPU per parameter per block, measured -- and a
            // patch that recomputes on every set would pay for it six times a
            // block for nothing.
            if(primed_ && params[i] == last_[i] && !(scaled && toggle_moved))
                continue;
            last_[i] = params[i];

            const float value = scaled ? (params[i] - 0.5f) * 2.f
                                             * spec_.toggle_range.span[toggle]
                                       : m.lo + params[i] * (m.hi - m.lo);

            Send(m.index, value);
        }
        primed_ = true;

        // With RNBO_USE_FLOAT32 a SampleValue is a float, so the chain's own
        // buffers go straight in and out with no conversion. The patch is happy
        // to work in place.
        const RNBO::SampleValue* ins[2]  = {left, right};
        RNBO::SampleValue*       outs[2] = {left, right};

        patcher_->process(ins, 2, outs, 2, size);
    }

    const EffectDesc& Desc() const override { return *spec_.desc; }

  private:
    /** Push a knob at a patch parameter, already scaled to its range. */
    void Send(int index, float value)
    {
        patcher_->setParameterValue(static_cast<RNBO::ParameterIndex>(index),
                                    static_cast<RNBO::ParameterValue>(value),
                                    RNBO::RNBOTimeNow);
    }

    const RnboPatchSpec& spec_;
    Patcher*             patcher_ = nullptr;

    bool  ready_                     = false;
    bool  primed_                    = false;
    int   last_toggle_               = -1;
    float last_[Effect::kParamCount] = {};
};

#endif
