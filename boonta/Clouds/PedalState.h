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
 *  One field breaks the strict one-way flow: SetOutputLevel() is written from
 *  the audio callback with the peak the DSP just produced, so the LEDs can act
 *  as a meter. It is telemetry, not control -- nothing reads it back into the
 *  DSP -- and it obeys the same single-writer rule as everything else.
 */
class PedalState
{
  public:
    /** Knobs named by what they mean, not where they are. The DSP asks for
     *  PARAM_POSITION; it never asks for KNOB_1. Re-point the mapping in
     *  Controls.cpp. */
    enum Param
    {
        PARAM_POSITION, /**< where in the buffer grains are drawn from */
        PARAM_SIZE,     /**< grain length */
        PARAM_PITCH,    /**< grain transposition, +/- 2 octaves */
        PARAM_DENSITY,  /**< grain rate, and how random the spacing is */
        PARAM_TEXTURE,  /**< grain window, boxy to smooth */
        PARAM_MIX,      /**< dry/wet */
        PARAM_LAST,
    };

    /** Three-way toggle meanings. */
    enum Toggle
    {
        TOGGLE_SPREAD,   /**< stereo scatter of the grain cloud */
        TOGGLE_FEEDBACK, /**< how much of the wet output re-enters the buffer */
        TOGGLE_REVERB,   /**< plate after the grains */
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

    /** Playback mode, one per select LED. Clouds' modes, less the spectral
     *  one: the same buffer and the same grain pool, scheduled three
     *  different ways. */
    enum Mode
    {
        MODE_GRANULAR, /**< randomised cloud */
        MODE_STRETCH,  /**< deterministic overlap-add time stretch */
        MODE_DELAY,    /**< pitch-shifting looping delay */
        MODE_LAST,
    };

    static constexpr int kModeCount = MODE_LAST;

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
    void SetMode(int mode);
    void NextMode() { SetMode(mode_ + 1); }
    void PrevMode() { SetMode(mode_ - 1); }

    // --- written by the DSP (telemetry; see the class comment) ----------
    void SetOutputLevel(float peak) { output_level_ = peak; }

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
    Mode      GetMode() const { return static_cast<Mode>(mode_); }
    float     GetOutputLevel() const { return output_level_; }

  private:
    float     param_[PARAM_LAST];
    float     expression_;
    TogglePos toggle_[TOGGLE_LAST];
    bool      bypassed_;
    bool      frozen_;
    bool      freeze_latched_;
    bool      freeze_remote_;
    int       mode_;
    float     output_level_;
};

#endif
