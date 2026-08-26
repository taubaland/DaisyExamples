#pragma once
#ifndef BOONTA_STORAGE_H
#define BOONTA_STORAGE_H

#include "daisy_boonta.h"
#include "util/PersistentStorage.h"

#include "PedalState.h"
#include "SavedState.h"

class Controls;

/** Sixteen presets in the Seed's QSPI flash, and the settings that survive a
 *  power cycle.
 *
 *  Restoring is easy; knowing *when* to save is the whole problem. A knob being
 *  turned changes the model on every audio block, and each save is a flash erase
 *  followed by a write -- slow, blocking, and finite: the chip is good for on
 *  the order of 100k erase cycles per sector. Saving per change would wear it
 *  out in an afternoon and stall the main loop continuously.
 *
 *  So the save is debounced. Changes are noticed, but nothing is written until
 *  the pedal has been left alone for a couple of seconds. Turn a knob across its
 *  whole range and that is one erase, not a thousand.
 *
 *  Main-loop context only. The erase blocks for long enough that calling this
 *  from the audio callback would drop buffers; the LEDs pause for a moment
 *  instead, which nobody can see.
 */
class Storage
{
  public:
    Storage(daisy::QSPIHandle& qspi)
    : store_(qspi), state_(nullptr), controls_(nullptr)
    {
    }

    /** Restore the current preset into the model, or leave it at its defaults
     *  if nothing valid is stored. Call before Controls::Init so the first pass
     *  parks the knobs against the restored values. */
    void Init(PedalState* state, Controls* controls);

    /** Autosave, and act on any pending preset recall. Main loop. */
    void Update(const PedalState& state);

    /** Ask for a preset. Safe from the audio callback: it only sets an index,
     *  and the switch itself happens in Update(). */
    void RequestPreset(int index);

    int  CurrentPreset() const { return current_; }
    bool Restored() const { return restored_; }

  private:
    void SaveNow(const SavedState& now);
    void SwitchTo(int index);

    daisy::PersistentStorage<SavedBank> store_;
    PedalState*                         state_;
    Controls*                           controls_;

    /** What is in the current slot on the chip, and what the model looked like
     *  last time round - see Update() for why both are needed. */
    SavedState last_saved_{};
    SavedState last_seen_{};

    int      current_       = 0;
    volatile int requested_ = -1;
    uint32_t changed_at_ms_ = 0;
    bool     dirty_         = false;
    bool     restored_      = false;
};

#endif
