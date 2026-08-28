#pragma once
#ifndef BOONTA_GEN_EFFECT_H
#define BOONTA_GEN_EFFECT_H

#include "Effect.h"

/** Adapter for a Max/MSP `gen~` or RNBO patch exported as C++.
 *
 *  ## What this can and cannot do
 *
 *  Both gen~'s *Export Code* and RNBO's C++ export are **code generators**: the
 *  artefact is C++ source that you compile in. Neither is a runtime format, so
 *  there is nothing to parse on the pedal and no interpreter to parse it with.
 *  Adding a patch means exporting it, dropping the sources beside this file,
 *  and rebuilding -- not copying a file onto the device.
 *
 *  What that buys is real: any patch you can write in gen~ becomes a slot, with
 *  the pedal's knobs, pages, presets, MIDI and soft pickup already attached.
 *
 *  ## Wiring one up
 *
 *  1. Export the patch. Put the generated sources in this directory and add
 *     them to CPP_SOURCES in the Makefile.
 *  2. Set BOONTA_GEN_EFFECT to the exported namespace, and fill in kGenDesc
 *     below with a name, a colour and six parameter names.
 *  3. Map the six knobs onto the patch's parameters in ParamIndex().
 *
 *  ## The parameter problem
 *
 *  A page is exactly six knobs, because the pedal has six pots. A patch has
 *  however many parameters it has. So the adapter needs a declared six-way
 *  subset -- that is what ParamIndex() is. Anything the patch exposes beyond
 *  those six keeps whatever value the patch was exported with.
 *
 *  Values arrive here as 0 to 1, because that is what a pot produces. gen~
 *  parameters usually want their own range, so scale in Apply() rather than
 *  expecting the patch to be authored in normalised units.
 *
 *  ## Before putting one in the signal path
 *
 *  Measure it. Three hand-written effects already cost about a third of the
 *  audio budget; generated DSP is not written to be lean, and a patch that is
 *  comfortable in Max on a desktop can be far too expensive here. Build with
 *  `C_DEFS += -DPROFILE_CPU` and read the load over the ST-Link -- the numbers
 *  are in the README -- before deciding it fits, let alone three of them.
 */

#ifdef BOONTA_GEN_EFFECT

// The exported code. gen~ puts everything in a namespace named after the
// patch; RNBO exports a class. Either way this include is the only place that
// knows which.
#include BOONTA_GEN_EFFECT_HEADER

class GenEffect : public Effect
{
  public:
    void Init(float sample_rate) override;
    void Process(const float* params, int toggle,
                 float* left, float* right, size_t size) override;
    const EffectDesc& Desc() const override;

  private:
    /** Which of the patch's parameters each of the six knobs drives. */
    static int ParamIndex(int knob);

    /** One knob's 0-1 value, scaled to what that patch parameter expects. */
    static float Scale(int knob, float value);

    void* state_ = nullptr;
    float sample_rate_ = 48000.f;
    float last_[Effect::kParamCount] = {};
    bool  primed_ = false;
};

#endif // BOONTA_GEN_EFFECT
#endif
