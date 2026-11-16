#pragma once
#ifndef BOONTA_PERSIST_H
#define BOONTA_PERSIST_H

#include "daisy_boonta.h"
#include "util/PersistentStorage.h"

#include "PedalState.h"

/** Remembers the pedal across a power cycle.
 *
 *  The controller that is neither a hand nor a MIDI cable: it watches the
 *  model, and when it has stopped changing it writes what it holds to the
 *  QSPI flash. At boot it reads that back into the model before the panel is
 *  looked at.
 *
 *  ## Where
 *
 *  Seven megabytes into the 8 MB flash, a long way past the application
 *  image the bootloader loads from the start of it. libDaisy's
 *  PersistentStorage does the erase and write; one 4 KB sector is used.
 *
 *  Erasing and writing flash is slow -- tens of milliseconds, occasionally
 *  hundreds -- and blocks its caller. So Update() belongs in the main loop,
 *  never the audio callback. The audio callback keeps running while it
 *  waits, because under BOOT_SRAM the whole program is in RAM and nothing it
 *  executes comes from the flash being written.
 *
 *  ## When
 *
 *  Not on every change: a knob sweep is hundreds of changes and flash is good
 *  for about a hundred thousand erases. A change has to stand for kSettleMs
 *  before it is written, and no two writes are closer than kMinGapMs, so the
 *  worst case is a write every ten seconds and a sweep costs one. Power
 *  removed inside that window loses the last change; nothing else is lost.
 *
 *  ## What
 *
 *  PedalState::Saved: knobs, toggle positions and the pitch quantisation. Not bypass,
 *  which always boots out of circuit, and not freeze.
 *
 *  Settings written by a different firmware, or by nothing sensible, are
 *  refused by PedalState::Restore() and the pedal starts from its defaults.
 *  The next change then overwrites them.
 */
class Persist
{
  public:
    explicit Persist(daisy::QSPIHandle& qspi)
    : storage_(qspi), state_(nullptr), dirty_(false), changed_at_(0), saved_at_(0)
    {
    }

    /** Read the saved state into the model, if there is a believable one.
     *  Call after the board is up and before Controls::Init().
     *  \return true if the model was restored. */
    bool Init(PedalState* state);

    /** Write the model out if it has changed and settled. Main loop. */
    void Update(uint32_t now_ms);

  private:
    daisy::PersistentStorage<PedalState::Saved> storage_;
    PedalState*        state_;
    PedalState::Saved  last_;
    bool               dirty_;
    uint32_t           changed_at_;
    uint32_t           saved_at_;
};

#endif
