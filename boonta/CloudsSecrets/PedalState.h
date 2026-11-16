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

    /** Playback mode. Clouds' four. The first is the pedal as it powers up;
     *  the other three are the hidden ones, reached by holding SEL_2 for five
     *  seconds (see Controls.cpp) and cycled with the select buttons. Three
     *  select LEDs cover them because the hidden three are only ever shown
     *  while the hidden layer is open: see IsAltOpen(). */
    enum Mode
    {
        MODE_GRANULAR, /**< randomised cloud */
        MODE_STRETCH,  /**< deterministic overlap-add time stretch */
        MODE_DELAY,    /**< pitch-shifting looping delay */
        MODE_SPECTRAL, /**< spectral madness: a phase vocoder, misused */
        MODE_LAST,
    };

    /** How many modes the select buttons can reach once the hidden layer is
     *  open: everything but granular. */
    static constexpr int kAltModeCount = MODE_LAST - 1;

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

    /** Pick a mode directly, from MIDI. Any mode may be chosen; choosing a
     *  hidden one opens the hidden layer, choosing granular closes it. */
    void SetMode(int mode);

    /** Select buttons. Does nothing until the hidden layer is open, then
     *  steps through the three hidden modes and wraps. */
    void NextMode() { StepAlt(+1); }
    void PrevMode() { StepAlt(-1); }

    /** The five-second hold: open the hidden layer (landing on the first
     *  hidden mode) or close it again (back to granular). */
    void ToggleAlt();

    // --- what survives a power cycle -------------------------------------

    /** The part of the model that is worth keeping across a power cycle: the
     *  knob values, the toggle positions and the playback mode. Not bypass, which
     *  always boots out of circuit, not freeze, and not telemetry.
     *
     *  Plain data with no padding, so two of them compare field by field and
     *  a stored one can be checked before it is believed. The magic number
     *  keeps one firmware from picking up another's settings, since they all
     *  share one place in flash. */
    struct Saved
    {
        uint32_t magic;
        float    param[PARAM_LAST];
        uint8_t  toggle[TOGGLE_LAST];
        uint8_t  mode;

        bool operator==(const Saved& o) const
        {
            if(magic != o.magic || mode != o.mode)
                return false;
            for(int i = 0; i < PARAM_LAST; i++)
                if(param[i] != o.param[i])
                    return false;
            for(int i = 0; i < TOGGLE_LAST; i++)
                if(toggle[i] != o.toggle[i])
                    return false;
            return true;
        }
        bool operator!=(const Saved& o) const { return !(*this == o); }
    };

    static constexpr uint32_t kSavedMagic = 0x43534543u;

    /** The current state, in storable form. */
    Saved Capture() const;

    /** Take stored state back. Returns false, and changes nothing, unless
     *  every field is believable: right magic, every value a number in range,
     *  every position one that exists. Flash that has been erased, half
     *  written or written by something else must not be able to put the
     *  pedal in a state nobody chose. */
    bool Restore(const Saved& saved);

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
    bool  FreezeLatched() const { return freeze_latched_ || freeze_remote_; }
    Mode  GetMode() const { return static_cast<Mode>(mode_); }

    /** True while the select buttons are choosing between hidden modes. */
    bool IsAltOpen() const { return mode_ != MODE_GRANULAR; }
    float GetOutputLevel() const { return output_level_; }

  private:
    void StepAlt(int dir);

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
