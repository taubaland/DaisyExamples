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
 */
class PedalState
{
  public:
    /** Knobs named by what they mean, not where they are. This is the layer
     *  that makes an effect readable: the DSP asks for PARAM_DRIVE, it never
     *  asks for KNOB_1. Re-point the mapping in Controls.cpp. */
    enum Param
    {
        PARAM_DRIVE,
        PARAM_TONE,
        PARAM_LEVEL,
        PARAM_MIX,
        PARAM_TIME,
        PARAM_FEEDBACK,
        PARAM_LAST,
    };

    /** Three-way toggle meanings. */
    enum Toggle
    {
        TOGGLE_RANGE,
        TOGGLE_CHARACTER,
        TOGGLE_ROUTING,
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

    static constexpr int kPresetCount = 3; /**< One per select LED. */

    PedalState() { Reset(); }

    void Reset();

    // --- written by the Controller -------------------------------------
    void SetParam(Param p, float value) { param_[p] = value; }
    void SetExpression(float value) { expression_ = value; }
    void SetToggle(Toggle t, TogglePos pos) { toggle_[t] = pos; }
    void SetBypass(bool bypassed) { bypassed_ = bypassed; }
    void ToggleBypass() { bypassed_ = !bypassed_; }
    void SetAltHeld(bool held) { alt_held_ = held; }
    void SetPreset(int preset);
    void NextPreset() { SetPreset(preset_ + 1); }
    void PrevPreset() { SetPreset(preset_ - 1); }
    void SetTapIntervalMs(float ms) { tap_interval_ms_ = ms; }

    // --- read by the View and the Effect -------------------------------
    float     GetParam(Param p) const { return param_[p]; }
    float     GetExpression() const { return expression_; }
    TogglePos GetToggle(Toggle t) const { return toggle_[t]; }
    bool      IsBypassed() const { return bypassed_; }
    bool      IsActive() const { return !bypassed_; }
    bool      AltHeld() const { return alt_held_; }
    int       GetPreset() const { return preset_; }
    float     GetTapIntervalMs() const { return tap_interval_ms_; }

  private:
    float     param_[PARAM_LAST];
    float     expression_;
    TogglePos toggle_[TOGGLE_LAST];
    bool      bypassed_;
    bool      alt_held_;
    int       preset_;
    float     tap_interval_ms_;
};

#endif
