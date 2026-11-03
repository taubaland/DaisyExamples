#pragma once
#ifndef BOONTA_PEDAL_STATE_H
#define BOONTA_PEDAL_STATE_H

#include <stdint.h>

/** Model.
 *
 *  The single source of truth for what the pedal is currently set to. It
 *  deliberately knows nothing about libDaisy: no pins, no ADC, no LED driver.
 *  The Controller writes it, the View and the Effect read it. Being plain data
 *  it also compiles on a host, so control logic can be unit tested off-target.
 *
 *  Threading: Controls::Process() writes this from the audio callback while
 *  LedView::Update() reads it from the main loop. Every member is a naturally
 *  aligned, word-sized POD written by exactly one context, so the worst case
 *  for the reader is one frame of stale LED colour.
 *
 *  Two fields break the strict one-way flow: SetOutputLevel() and
 *  SetInputPeak() are written from the audio callback so the LEDs can act as
 *  a meter and a clip indicator. They are telemetry, not control -- nothing
 *  reads them back into the DSP -- and they obey the same single-writer rule
 *  as everything else.
 */
class PedalState
{
  public:
    /** Knobs named by what they mean, not where they are. The DSP asks for
     *  PARAM_TIME; it never asks for KNOB_1. Re-point the mapping in
     *  Controls.cpp. */
    enum Param
    {
        PARAM_TIME,    /**< how far back in the buffer grains are drawn from */
        PARAM_SIZE,    /**< grain length */
        PARAM_PITCH,   /**< grain transposition, +/- 2 octaves */
        PARAM_DENSITY, /**< grain rate, and how clocked it is */
        PARAM_SHAPE,   /**< grain envelope, percussive to reversed */
        PARAM_MIX,     /**< dry/wet */
        PARAM_LAST,
    };

    /** Three-way toggle meanings. */
    enum Toggle
    {
        TOGGLE_SEED,     /**< per-grain randomisation of pitch, pan, position */
        TOGGLE_FEEDBACK, /**< how much of the wet output re-enters the buffer */
        TOGGLE_QUALITY,  /**< what the recording path does to the signal */
        TOGGLE_LAST,
    };

    /** Toggle position, decoupled from Switch3::POS_* so the model stays
     *  hardware agnostic. */
    enum TogglePos
    {
        POS_LOW,
        POS_MID,
        POS_HIGH,
    };

    /** Pitch quantisation, one per select LED. Beads hides this in its alt
     *  settings; here it is on the front panel, because with the SEED toggle
     *  scattering pitch it is the difference between a chord and a mess. */
    enum Quantise
    {
        QUANT_FREE,      /**< continuous */
        QUANT_CHROMATIC, /**< nearest semitone */
        QUANT_HARMONIC,  /**< octaves and fifths only */
        QUANT_LAST,
    };

    static constexpr int kQuantiseCount = QUANT_LAST;

    PedalState() { Reset(); }

    void Reset();

    // --- written by the Controller -------------------------------------
    void SetParam(Param p, float value) { param_[p] = value; }
    void SetExpression(float value) { expression_ = value; }
    void SetToggle(Toggle t, TogglePos pos) { toggle_[t] = pos; }
    void SetBypass(bool bypassed) { bypassed_ = bypassed; }
    void ToggleBypass() { bypassed_ = !bypassed_; }
    void SetFreeze(bool frozen) { frozen_ = frozen; }
    void SetFreezeLatched(bool latched) { freeze_latched_ = latched; }

    /** Freeze asked for over MIDI. Held apart from the footswitch's own
     *  flag rather than sharing one, because the two are independent
     *  sources and either should be able to freeze on its own: the switch
     *  sitting up must not cancel a controller that is holding freeze
     *  down, and a controller must not disable the switch. */
    void SetFreezeRemote(bool frozen) { freeze_remote_ = frozen; }
    void SetQuantise(int q);
    void NextQuantise() { SetQuantise(quantise_ + 1); }
    void PrevQuantise() { SetQuantise(quantise_ - 1); }

    // --- written by the DSP (telemetry; see the class comment) ----------
    void SetOutputLevel(float peak) { output_level_ = peak; }
    void SetInputPeak(float peak) { input_peak_ = peak; }

    // --- read by the View and the Effect -------------------------------
    float     GetParam(Param p) const { return param_[p]; }
    float     GetExpression() const { return expression_; }
    TogglePos GetToggle(Toggle t) const { return toggle_[t]; }
    bool      IsBypassed() const { return bypassed_; }
    bool      IsActive() const { return !bypassed_; }
    bool      IsFrozen() const { return frozen_ || freeze_remote_; }

    /** True when freeze will stay on with nothing held down -- latched on
     *  the footswitch, or asserted by a controller. The LED uses this to
     *  tell a freeze you can walk away from from one you cannot. */
    bool FreezeLatched() const { return freeze_latched_ || freeze_remote_; }
    Quantise  GetQuantise() const { return static_cast<Quantise>(quantise_); }
    float     GetOutputLevel() const { return output_level_; }
    float     GetInputPeak() const { return input_peak_; }

  private:
    float     param_[PARAM_LAST];
    float     expression_;
    TogglePos toggle_[TOGGLE_LAST];
    bool      bypassed_;
    bool      frozen_;
    bool      freeze_latched_;
    bool      freeze_remote_;
    int       quantise_;
    float     output_level_;
    float     input_peak_;
};

#endif
