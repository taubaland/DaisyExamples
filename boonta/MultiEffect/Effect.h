#pragma once
#ifndef BOONTA_EFFECT_H
#define BOONTA_EFFECT_H

#include <stddef.h>
#include <stdint.h>

/** What every effect looks like from the outside.
 *
 *  The point of this file is that an effect no longer knows what a page is.
 *  It is handed six numbers between 0 and 1, a three-position toggle, and a
 *  stereo buffer. Where those numbers came from -- a pot, MIDI, a preset, a
 *  test harness -- is not its business, and neither is which slot it happens to
 *  be sitting in.
 *
 *  That is what makes the slots interchangeable. Before this, each effect
 *  reached into the model by name (`state.Eq(EQ_LOW_FREQ)`), which meant the
 *  EQ could only ever be the EQ.
 *
 *  Effects are constructed once, statically, and live for the life of the
 *  program. Nothing here allocates.
 */

/** One parameter, for the page it appears on. Names are for documentation and
 *  for anything that ever puts them on a screen; the DSP addresses them by
 *  index, so the order of these *is* the knob order. */
struct ParamSpec
{
    const char* name;
};

/** Everything the rest of the pedal needs to know about an effect without
 *  knowing what it is: what to call it, what colour to light for it, what its
 *  toggle does, and what its six knobs mean. */
struct EffectDesc
{
    const char* name;
    float       r, g, b;      /**< LED colour, wherever this effect appears */
    const char* toggle;       /**< what the slot's toggle selects, for docs  */
    ParamSpec   param[6];
};

class Effect
{
  public:
    /** Six knobs per page, so six parameters per effect. Not a limit that
     *  wants raising casually: it is the number of pots on the pedal. */
    static constexpr int kParamCount = 6;

    virtual ~Effect() {}

    virtual void Init(float sample_rate) = 0;

    /** Process one block in place.
     *  \param params  kParamCount values, 0 to 1, in the order of Desc().param
     *  \param toggle  this slot's toggle, 0 = down, 1 = centre, 2 = up
     */
    virtual void Process(const float* params,
                         int          toggle,
                         float*       left,
                         float*       right,
                         size_t       size)
        = 0;

    virtual const EffectDesc& Desc() const = 0;
};

#endif
