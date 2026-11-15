#pragma once
#ifndef BOONTA_PEDAL_STATE_H
#define BOONTA_PEDAL_STATE_H

#include <stdint.h>

/** Model.
 *
 *  The single source of truth for what the pedal is currently set to. It
 *  deliberately knows nothing about libDaisy: no pins, no ADC, no LED driver.
 *  The Controllers write it, the View and the Effect read it. Being plain data
 *  it also compiles on a host, so control logic can be unit tested off-target.
 *
 *  Threading: Controls::Process() and MidiControl::Process() write this from
 *  the audio callback while LedView::Update() reads it from the main loop.
 *  Every member is a naturally aligned, word-sized POD written by exactly one
 *  context, so the worst case for the reader is one frame of stale LED colour.
 *
 *  Two fields break the strict one-way flow: SetOutputLevel() and
 *  SetSounding() are written from the audio callback with what the DSP just
 *  did, so the LEDs can show it. They are telemetry, not control -- nothing
 *  reads them back into the DSP.
 */
class PedalState
{
  public:
    /** Elements has eighteen things worth a knob and the pedal has six, so
     *  the knobs are paged: the three select LEDs are three pages of six.
     *  The order here *is* the layout -- parameter n is knob n % 6 on page
     *  n / 6 -- and the MIDI map and the saved state both follow it. */
    enum Param
    {
        // --- page 0: the resonator ---------------------------------------
        PARAM_PITCH,      /**< coarse, in semitones over four octaves */
        PARAM_GEOMETRY,   /**< plate, bar, string... or the chord */
        PARAM_BRIGHTNESS, /**< how fast the high partials die */
        PARAM_DAMPING,    /**< how fast everything dies */
        PARAM_POSITION,   /**< where the structure is struck */
        PARAM_SPACE,      /**< stereo width, then reverb */

        // --- page 1: what excites it, and how hard -----------------------
        PARAM_CONTOUR,  /**< envelope shape of the bow and blow exciters */
        PARAM_BOW,      /**< bow level */
        PARAM_BLOW,     /**< blow level; past two thirds it adds the tube */
        PARAM_STRIKE,   /**< strike level; past 0.8 it bleeds through raw */
        PARAM_INPUT,    /**< how much of the input jack reaches the exciter */
        PARAM_STRENGTH, /**< how hard a note is played */

        // --- page 2: the colour of each exciter --------------------------
        PARAM_BOW_TIMBRE,
        PARAM_FLOW, /**< blow meta: the texture of the air */
        PARAM_BLOW_TIMBRE,
        PARAM_MALLET, /**< strike meta: samples, mallets, particles */
        PARAM_STRIKE_TIMBRE,
        PARAM_FINE, /**< +/- one semitone */

        PARAM_LAST,
    };

    static constexpr int kKnobsPerPage = 6;
    static constexpr int kPageCount    = PARAM_LAST / kKnobsPerPage;

    /** Three-way toggle meanings. */
    enum Toggle
    {
        TOGGLE_MODEL,   /**< modal / string / chord of strings */
        TOGGLE_TRIGGER, /**< what plays a note; see TRIGGER_* */
        TOGGLE_ROUTE,   /**< where the input jack goes; see ROUTE_* */
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

    /** TOGGLE_TRIGGER. The footswitch and MIDI notes always play; this is
     *  what else does. */
    static constexpr TogglePos TRIGGER_MANUAL = POS_LOW; /**< nothing else */
    static constexpr TogglePos TRIGGER_INPUT  = POS_MID; /**< the input's own attacks */
    static constexpr TogglePos TRIGGER_DRONE  = POS_HIGH; /**< the gate is simply held */

    /** TOGGLE_ROUTE: where the input jack goes. */
    static constexpr TogglePos ROUTE_STRIKE = POS_LOW; /**< straight into the resonator */
    static constexpr TogglePos ROUTE_BOTH   = POS_MID;
    static constexpr TogglePos ROUTE_BLOW   = POS_HIGH; /**< through the diffuser first */

    /** TOGGLE_MODEL: which resonator. */
    static constexpr TogglePos MODEL_MODAL   = POS_LOW;
    static constexpr TogglePos MODEL_STRING  = POS_MID;
    static constexpr TogglePos MODEL_STRINGS = POS_HIGH; /**< a chord of five */

    PedalState() { Reset(); }

    void Reset();

    // --- written by the Controllers --------------------------------------
    void SetParam(Param p, float value) { param_[p] = value; }
    void SetExpression(float value) { expression_ = value; }
    void SetToggle(Toggle t, TogglePos pos) { toggle_[t] = pos; }
    void SetBypass(bool bypassed) { bypassed_ = bypassed; }
    void ToggleBypass() { bypassed_ = !bypassed_; }
    void SetPage(int page);
    void NextPage() { SetPage(page_ + 1); }
    void PrevPage() { SetPage(page_ - 1); }

    /** The gate footswitch. Held apart from the remote gate below for the
     *  reason Clouds keeps two freeze flags: they are independent sources
     *  and either should be able to play a note on its own. */
    void SetGate(bool gate) { gate_ = gate; }
    void SetGateLatched(bool latched) { gate_latched_ = latched; }

    /** Gate asked for by a controller (CC), as opposed to a note. */
    void SetGateRemote(bool gate) { gate_remote_ = gate; }

    /** MIDI notes. Last note wins; the gate drops when the last held key is
     *  released. The note persists after release, so a decaying sound keeps
     *  its pitch and the footswitch plays the last key pressed. */
    void NoteOn(uint8_t note, uint8_t velocity);
    void NoteOff(uint8_t note);
    void AllNotesOff() { held_notes_ = 0; }

    /** Pitch bend, -1 to +1. */
    void SetBend(float bend) { bend_ = bend; }

    // --- what survives a power cycle -------------------------------------

    /** Knobs, toggles and the page. Not bypass, which always boots out of
     *  circuit, not the gate, not notes and not telemetry. See Clouds'
     *  PedalState for why it is shaped like this. */
    struct Saved
    {
        uint32_t magic;
        float    param[PARAM_LAST];
        uint8_t  toggle[TOGGLE_LAST];
        uint8_t  page;

        bool operator==(const Saved& o) const
        {
            if(magic != o.magic || page != o.page)
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

    static constexpr uint32_t kSavedMagic = 0x454C454Du; // 'ELEM'

    Saved Capture() const;

    /** Take stored state back. Returns false, and changes nothing, unless
     *  every field is believable. */
    bool Restore(const Saved& saved);

    // --- written by the DSP and by boot (telemetry) ----------------------
    void SetOutputLevel(float peak) { output_level_ = peak; }
    void SetSounding(bool gate) { sounding_ = gate; }
    void SetSamplesLoaded(bool loaded) { samples_loaded_ = loaded; }

    // --- read by the View and the Effect ---------------------------------
    float     GetParam(Param p) const { return param_[p]; }
    float     GetExpression() const { return expression_; }
    TogglePos GetToggle(Toggle t) const { return toggle_[t]; }
    bool      IsBypassed() const { return bypassed_; }
    bool      IsActive() const { return !bypassed_; }
    int       GetPage() const { return page_; }

    /** The gate from everything that is not the input jack. */
    bool Gate() const { return gate_ || gate_remote_ || held_notes_ > 0; }

    /** True when the gate will stay up with nothing held down. */
    bool GateLatched() const { return gate_latched_ || gate_remote_; }

    /** Semitones the keyboard transposes by: the last key relative to
     *  middle C, so that with nothing plugged in the pitch knob means what
     *  it says. */
    float NoteOffset() const { return static_cast<float>(note_) - 60.f; }

    /** How hard the note in progress was played: the key's velocity while a
     *  key is down, and full otherwise, so a soft last note does not make
     *  the footswitch quiet. */
    float Velocity() const { return held_notes_ > 0 ? velocity_ : 1.f; }
    float Bend() const { return bend_; }

    float GetOutputLevel() const { return output_level_; }
    bool  IsSounding() const { return sounding_; }
    bool  SamplesLoaded() const { return samples_loaded_; }

  private:
    float     param_[PARAM_LAST];
    float     expression_;
    TogglePos toggle_[TOGGLE_LAST];
    bool      bypassed_;
    bool      gate_;
    bool      gate_latched_;
    bool      gate_remote_;
    int       page_;
    int       held_notes_;
    int       note_;
    float     velocity_;
    float     bend_;
    float     output_level_;
    bool      sounding_;
    bool      samples_loaded_;
};

#endif
