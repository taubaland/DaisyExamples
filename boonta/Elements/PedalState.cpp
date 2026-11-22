#include "PedalState.h"

/** Where every parameter sits before anything has touched it.
 *
 *  This matters more here than on a one-page pedal. Only page 0 is under the
 *  knobs at boot, so the other twelve parameters hold these values until
 *  somebody pages to them and turns something -- they have to add up to a
 *  sound on their own. They are Elements' own Part::Init() patch: a struck
 *  bar, a little air, no bow. */
static constexpr float kDefault[PedalState::PARAM_LAST] = {
    0.5f,  // PITCH: C3
    0.2f,  // GEOMETRY
    0.5f,  // BRIGHTNESS
    0.25f, // DAMPING
    0.3f,  // POSITION
    0.5f,  // SPACE
    1.0f,  // CONTOUR
    0.0f,  // BOW
    0.0f,  // BLOW
    0.8f,  // STRIKE
    0.5f,  // INPUT
    0.8f,  // STRENGTH
    0.5f,  // BOW_TIMBRE
    0.5f,  // FLOW
    0.5f,  // BLOW_TIMBRE
    0.5f,  // MALLET
    0.5f,  // STRIKE_TIMBRE
    0.5f,  // FINE: centred
};

void PedalState::Reset()
{
    for(int i = 0; i < PARAM_LAST; i++)
        param_[i] = kDefault[i];
    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = POS_MID;

    expression_     = 0.f;
    bypassed_       = true; // boot up out of circuit
    gate_           = false;
    gate_latched_   = false;
    gate_remote_    = false;
    page_           = 0;
    held_notes_     = 0;
    note_           = 60;
    velocity_       = 1.f;
    bend_           = 0.f;
    output_level_   = 0.f;
    sounding_       = false;
    samples_loaded_ = true;
}

void PedalState::SetPage(int page)
{
    // Wrap in both directions so the select buttons can cycle either way.
    page_ = ((page % kPageCount) + kPageCount) % kPageCount;
}

void PedalState::NoteOn(uint8_t note, uint8_t velocity)
{
    // A note-on at velocity zero is a note-off; running status depends on it.
    if(velocity == 0)
    {
        NoteOff(note);
        return;
    }

    note_     = note;
    velocity_ = static_cast<float>(velocity) / 127.f;
    held_notes_++;
}

void PedalState::NoteOff(uint8_t)
{
    // Counted rather than tracked per key: enough to know when the last one
    // has come up, and a lost note-off cannot leave a count below zero.
    if(held_notes_ > 0)
        held_notes_--;
}

PedalState::Saved PedalState::Capture() const
{
    Saved s;
    s.magic = kSavedMagic;

    for(int i = 0; i < PARAM_LAST; i++)
    {
        const float v = param_[i];
        s.param[i]    = (v >= 0.f && v <= 1.f) ? v : (v > 1.f ? 1.f : 0.f);
    }
    for(int i = 0; i < TOGGLE_LAST; i++)
        s.toggle[i] = static_cast<uint8_t>(toggle_[i]);

    s.page = static_cast<uint8_t>(page_);
    return s;
}

bool PedalState::Restore(const Saved& saved)
{
    if(saved.magic != kSavedMagic)
        return false;

    for(int i = 0; i < PARAM_LAST; i++)
    {
        const float v = saved.param[i];
        if(!(v >= 0.f && v <= 1.f)) // false for NaN as well
            return false;
    }
    for(int i = 0; i < TOGGLE_LAST; i++)
        if(saved.toggle[i] > POS_HIGH)
            return false;
    if(saved.page >= kPageCount)
        return false;

    for(int i = 0; i < PARAM_LAST; i++)
        param_[i] = saved.param[i];
    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = static_cast<TogglePos>(saved.toggle[i]);
    SetPage(saved.page);
    return true;
}
