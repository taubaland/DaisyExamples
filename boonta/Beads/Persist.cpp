#include "Persist.h"

/** Offset into the QSPI part: 7 MB of 8. The application image the bootloader
 *  loads starts at 256 KB and is a fraction of a megabyte, so the two cannot
 *  meet. Keep it clear of anything else that uses the flash. */
static constexpr uint32_t kFlashOffset = 0x700000;

/** A change must stand this long before it is written. */
static constexpr uint32_t kSettleMs = 2000;

/** No two writes closer than this. Flash wears per erase. */
static constexpr uint32_t kMinGapMs = 10000;

bool Persist::Init(PedalState* state)
{
    state_ = state;

    // The defaults are the model as it stands, which is Reset(). If nothing
    // has ever been written there PersistentStorage writes them out once and
    // reports the state as FACTORY.
    storage_.Init(state_->Capture(), kFlashOffset);

    bool restored = false;
    if(storage_.GetState()
       == daisy::PersistentStorage<PedalState::Saved>::State::USER)
        restored = state_->Restore(storage_.GetSettings());

    // Whatever the model holds now is what is already on record, or what is
    // about to be thrown away: either way it is not a change.
    last_  = state_->Capture();
    dirty_ = false;

    return restored;
}

void Persist::Update(uint32_t now_ms)
{
    const PedalState::Saved now = state_->Capture();

    if(now != last_)
    {
        last_       = now;
        changed_at_ = now_ms;
        dirty_      = true;
    }

    if(!dirty_)
        return;

    if(now_ms - changed_at_ < kSettleMs)
        return;

    if(saved_at_ != 0 && now_ms - saved_at_ < kMinGapMs)
        return;

    storage_.GetSettings() = now;
    storage_.Save();

    dirty_    = false;
    saved_at_ = daisy::System::GetNow();
}
