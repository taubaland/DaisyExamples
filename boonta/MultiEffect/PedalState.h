#pragma once
#ifndef BOONTA_PEDAL_STATE_H
#define BOONTA_PEDAL_STATE_H

#include <stdint.h>

/** Model.
 *
 *  The single source of truth for what the pedal is currently set to. It
 *  deliberately knows nothing about libDaisy: no pins, no ADC, no LED driver.
 *  The Controller writes it, the View and the Chain read it. Being plain data
 *  it also compiles on a host, so control logic can be unit tested off-target.
 *
 *  The multi-effect stores a full set of six values *per page*, so the six
 *  physical knobs are a window onto 24 parameters rather than the whole state.
 *  Leaving a page does not disturb what was set on it; see Controls.cpp for how
 *  a knob re-acquires a value it no longer physically matches.
 *
 *  Threading: Controls::Process() writes this from the audio callback while
 *  LedView::Update() reads it from the main loop. Every member is a naturally
 *  aligned, word-sized POD written by exactly one context, so the worst case
 *  for the reader is one frame of stale LED colour.
 */
class PedalState
{
  public:
    static constexpr int kKnobCount = 6;

    /** The four parameter pages, cycled by the left footswitch. */
    enum Page
    {
        PAGE_EQ,
        PAGE_DRIVE,
        PAGE_REVERB,
        PAGE_META,
        PAGE_LAST,
    };

    /** Knobs named by what they mean, not where they are. Every enum below
     *  indexes the same six physical knobs; which set is live depends on the
     *  page. The DSP asks for EQ_LOW_GAIN and never learns it was KNOB_4. */
    enum EqParam
    {
        EQ_LOW_FREQ,  /**< 40 Hz - 500 Hz  */
        EQ_MID_FREQ,  /**< 200 Hz - 4 kHz  */
        EQ_HIGH_FREQ, /**< 1.5 kHz - 12 kHz */
        EQ_LOW_GAIN,  /**< low shelf, +/-15 dB, centre detent is flat  */
        EQ_MID_GAIN,  /**< peaking band                                */
        EQ_HIGH_GAIN, /**< high shelf                                  */
    };

    enum DriveParam
    {
        DRIVE_GAIN,      /**< pre-shaper gain, scaled by the RANGE toggle */
        DRIVE_TONE,      /**< post lowpass, 250 Hz - 8 kHz                */
        DRIVE_CHARACTER, /**< soft clip -> hard clip -> wavefold          */
        DRIVE_BIAS,      /**< shaper asymmetry, centre is symmetric       */
        DRIVE_LEVEL,     /**< make-up gain, unity at centre               */
        DRIVE_MIX,       /**< dry/wet within the drive block              */
    };

    enum ReverbParam
    {
        REVERB_TIME,      /**< tank decay          */
        REVERB_DAMPING,   /**< HF loss per pass    */
        REVERB_PREDELAY,  /**< 0 - 250 ms          */
        REVERB_DIFFUSION, /**< allpass density     */
        REVERB_LOWCUT,    /**< input HPF, 20 Hz - 500 Hz */
        REVERB_MIX,       /**< dry/wet within the reverb block */
    };

    enum MetaParam
    {
        META_EQ_AMOUNT,     /**< KNOB_1: per-slot dry/wet                    */
        META_REVERB_AMOUNT, /**< KNOB_2                                      */
        META_IN_GAIN,       /**< KNOB_3: chain input trim, +/-12 dB at ends  */
        META_DRIVE_AMOUNT,  /**< KNOB_4                                      */
        META_OUT_LEVEL,     /**< KNOB_5: chain output, unity at centre       */
        META_MIX,           /**< KNOB_6: global dry/wet across the chain     */
    };

    /** The three effects. A slot is *what* an effect is; its position in the
     *  chain is a separate thing, looked up through SlotAt(). */
    enum Slot
    {
        SLOT_EQ,
        SLOT_DRIVE,
        SLOT_REVERB,
        SLOT_LAST,
    };

    /** Three effects permute six ways, cycled by the select buttons. */
    static constexpr int kOrderCount = 6;

    /** Three-way toggle meanings. Unlike the knobs these are global: a toggle
     *  reads the same whichever page is on screen, because a physical switch
     *  that changed meaning under your hand would be a liability live. */
    enum Toggle
    {
        TOGGLE_DRIVE_RANGE, /**< TOG_SW_1: gain range, low / mid / high  */
        TOGGLE_REVERB_SIZE, /**< TOG_SW_2: room / hall / cavern          */
        TOGGLE_EQ_Q,        /**< TOG_SW_3: band width, wide / med / tight */
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

    PedalState() { Reset(); }

    void Reset();

    // --- written by the Controller -------------------------------------
    void SetKnob(Page page, int knob, float value) { param_[page][knob] = value; }
    void SetExpression(float value) { expression_ = value; }
    void SetToggle(Toggle t, TogglePos pos) { toggle_[t] = pos; }
    void SetBypass(bool bypassed) { bypassed_ = bypassed; }
    void ToggleBypass() { bypassed_ = !bypassed_; }
    void SetSlotBypass(Slot s, bool bypassed) { slot_bypassed_[s] = bypassed; }
    void ToggleSlotBypass(Slot s) { slot_bypassed_[s] = !slot_bypassed_[s]; }
    void NextPage();
    void NextOrder();
    void PrevOrder();

    /** Used when restoring saved settings. Controls notices the page moved and
     *  parks the knobs against it, exactly as if you had walked there. */
    void SetPage(Page page) { page_ = page; }
    void SetOrder(int order);

    /** Bit i set means knob i has not yet been picked up on the current page,
     *  so the stored value is holding and the pot is ignored. */
    void SetPickupPending(uint32_t mask) { pickup_pending_ = mask; }

    /** Set false on arriving at a page, true once a knob there has actually
     *  moved a parameter. */
    void SetPageEdited(bool edited) { page_edited_ = edited; }

    // --- read by the View and the DSP ----------------------------------
    float GetKnob(Page page, int knob) const { return param_[page][knob]; }

    float Eq(EqParam p) const { return param_[PAGE_EQ][p]; }
    float Drive(DriveParam p) const { return param_[PAGE_DRIVE][p]; }
    float Reverb(ReverbParam p) const { return param_[PAGE_REVERB][p]; }
    float Meta(MetaParam p) const { return param_[PAGE_META][p]; }

    /** Per-slot dry/wet from the meta page, by slot rather than by knob, so the
     *  chain can loop over slots without a switch. The three amount knobs are
     *  interleaved with the gain knobs on that page, so this is a lookup rather
     *  than arithmetic. */
    float SlotAmount(Slot s) const;

    /** True when the effect has been switched out with a short press of the
     *  page footswitch. Independent of the amount knob: bypassing forces the
     *  slot fully dry whatever the knob says, and leaves the knob alone so
     *  switching back in restores what was there. */
    bool SlotBypassed(Slot s) const { return slot_bypassed_[s]; }

    /** What a slot contributes once bypass has had its say. */
    float EffectiveSlotAmount(Slot s) const
    {
        return slot_bypassed_[s] ? 0.f : SlotAmount(s);
    }

    /** The effect a page edits, or SLOT_LAST for the meta page, which edits the
     *  chain as a whole rather than any one effect. */
    static Slot PageSlot(Page page);

    Page      GetPage() const { return page_; }
    int       GetOrder() const { return order_; }
    /** Which effect sits at the given position in the chain, 0 = first. */
    Slot      SlotAt(int position) const;
    float     GetExpression() const { return expression_; }
    TogglePos GetToggle(Toggle t) const { return toggle_[t]; }
    bool      IsBypassed() const { return bypassed_; }
    bool      IsActive() const { return !bypassed_; }
    uint32_t  PickupPending() const { return pickup_pending_; }

    /** True once a parameter on the current page has actually been changed since
     *  you arrived at it.
     *
     *  Deliberately not "a knob has been picked up": a pot that happens to sit
     *  on its stored value picks up on the first block, which would clear the
     *  indicator without anything having been edited. */
    bool      PageEdited() const { return page_edited_; }

  private:
    float     param_[PAGE_LAST][kKnobCount];
    float     expression_;
    TogglePos toggle_[TOGGLE_LAST];
    Page      page_;
    int       order_;
    bool      bypassed_;
    bool      slot_bypassed_[SLOT_LAST];
    bool      page_edited_;
    uint32_t  pickup_pending_;
};

#endif
