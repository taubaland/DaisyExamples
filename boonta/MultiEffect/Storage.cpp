#include "Storage.h"

#include "Controls.h"

using namespace daisy;

/** How long the pedal has to be left alone before a change is written. Long
 *  enough that a knob sweep is one save rather than hundreds; short enough that
 *  you are unlikely to pull the plug inside the window. */
static constexpr uint32_t kSettleMs = 2000;

void Storage::Init(PedalState* state, Controls* controls)
{
    state_    = state;
    controls_ = controls;

    // Defaults are whatever the model already holds -- PedalState::Reset() has
    // run in its constructor -- in every slot, so a first-ever boot writes a
    // coherent bank rather than leaving the chip in whatever state it shipped
    // in.
    SavedBank defaults{};
    defaults.version = SavedBank::kVersion;
    defaults.current = 0;
    for(int i = 0; i < SavedBank::kPresetCount; i++)
        defaults.preset[i] = CaptureState(*state);

    store_.Init(defaults);

    SavedBank& bank = store_.GetSettings();

    restored_ = false;
    if(bank.version == SavedBank::kVersion && bank.current >= 0
       && bank.current < SavedBank::kPresetCount)
    {
        current_  = bank.current;
        restored_ = ApplyState(bank.preset[current_], state);
    }
    else
    {
        // Either nothing has ever been stored, or what is there was written by
        // a layout this build does not understand. Overwrite it now rather than
        // rejecting it again on every future boot -- otherwise a version bump
        // leaves the pedal permanently unable to remember anything, which looks
        // exactly like persistence being broken.
        bank     = defaults;
        current_ = 0;
        store_.Save();
    }

    // Whether or not it was usable, the model is now the reference: start both
    // detectors from here so a restore is not immediately re-saved, and a
    // rejected block is not mistaken for a change on the next Update().
    last_seen_  = CaptureState(*state);
    last_saved_ = last_seen_;
    dirty_      = false;
}

void Storage::RequestPreset(int index)
{
    if(index >= 0 && index < SavedBank::kPresetCount)
        requested_ = index;
}

void Storage::SaveNow(const SavedState& now)
{
    SavedBank& bank        = store_.GetSettings();
    bank.version           = SavedBank::kVersion;
    bank.current           = current_;
    bank.preset[current_]  = now;
    store_.Save();
    last_saved_ = now;
    dirty_      = false;
}

void Storage::SwitchTo(int index)
{
    // Leave the slot you are on as you left it, including edits still inside
    // the settle window -- otherwise recalling a preset silently discards the
    // last couple of seconds of tweaking.
    const SavedState now = CaptureState(*state_);
    if(now != last_saved_)
        SaveNow(now);

    current_ = index;

    SavedBank& bank = store_.GetSettings();
    if(!ApplyState(bank.preset[current_], state_))
        return; // slot never written or unreadable; leave the model alone

    // The model has been replaced wholesale, so every pot is now pointing
    // somewhere the parameters no longer are. Park them against the new values
    // or a picked-up knob overwrites what was just loaded.
    if(controls_)
        controls_->RePark();

    last_seen_  = CaptureState(*state_);
    last_saved_ = last_seen_;
    dirty_      = false;

    bank.current = current_;
    store_.Save();
}

void Storage::Update(const PedalState& state)
{
    const int req = requested_;
    if(req >= 0)
    {
        requested_ = -1;
        if(req != current_)
        {
            SwitchTo(req);
            return;
        }
    }

    const SavedState now = CaptureState(state);

    // Two separate questions, and conflating them was a bug worth naming.
    //
    // "Does this differ from what is on the chip?" is asked against the last
    // thing saved. "Has it stopped moving?" is asked against the last thing
    // seen. An earlier version asked only the second and restarted the timer
    // whenever the model differed at all, so a single picked-up knob -- whose
    // value is rewritten from a jittering pot every block -- kept the timer
    // permanently reset and nothing was ever written.
    dirty_ = now != last_saved_;

    if(!dirty_)
        return;

    if(now != last_seen_)
    {
        last_seen_     = now;
        changed_at_ms_ = System::GetNow();
        return;
    }

    if(System::GetNow() - changed_at_ms_ < kSettleMs)
        return;

    // Settled. PersistentStorage compares against what is already on the chip
    // and skips the erase if it matches anyway.
    SaveNow(now);
}
