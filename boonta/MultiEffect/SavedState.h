#pragma once
#ifndef BOONTA_SAVED_STATE_H
#define BOONTA_SAVED_STATE_H

#include <stdint.h>

#include "PedalState.h"

/** What survives a power cycle, and the pure conversion either way.
 *
 *  Deliberately free of libDaisy so the round trip can be tested on a host --
 *  the QSPI plumbing lives in Storage, which is the only part that needs
 *  hardware.
 *
 *  Only settings are saved. Anything that describes the moment rather than the
 *  configuration is left out: the expression pedal reading, which knobs are
 *  parked, and whether the page has been edited are all rebuilt at boot from
 *  the hardware itself.
 *
 *  The toggles are not saved either, and cannot usefully be: they are physical
 *  three-position switches, so their position at power-on *is* the truth.
 */
struct SavedState
{
    /** Bumped whenever the layout below changes. Storage refuses to restore a
     *  block with a version it does not recognise and falls back to defaults,
     *  which is the difference between "settings reset" and a pedal booting
     *  with garbage in its parameters. */
    static constexpr uint32_t kVersion = 2;

    uint32_t version;
    float    param[PedalState::PAGE_LAST][PedalState::kKnobCount];
    int32_t  page;
    int32_t  order;
    uint8_t  slot_bypassed[PedalState::SLOT_LAST];
    uint8_t  bypassed;

    /** Which effect is in each slot, by registry id.
     *
     *  Without this a preset is meaningless the moment slots are
     *  interchangeable: six numbers restored into whichever effect happens to
     *  be loaded would set a reverb's decay from a drive's bias. */
    uint8_t  slot_effect[PedalState::SLOT_LAST];

    /** How far a parameter must differ before the two blocks count as unalike.
     *
     *  This has to be a tolerance and not an exact comparison. A picked-up knob
     *  is rewritten from its pot on every audio block, and the smoothed ADC
     *  value wanders in the last few decimal places forever. Compared exactly,
     *  a pedal sitting untouched on the bench looks like it is being changed a
     *  thousand times a second: the save never settles, so nothing is ever
     *  written, and PersistentStorage would erase the flash continuously if it
     *  were. Two parts in a thousand is far above that noise and far below any
     *  setting you could hear. */
    static constexpr float kEpsilon = 0.002f;

    /** PersistentStorage compares with this to decide whether an erase/write
     *  cycle is actually needed, so it is what stops the flash being rewritten
     *  with effectively identical data. */
    bool operator!=(const SavedState& o) const
    {
        if(version != o.version || page != o.page || order != o.order
           || bypassed != o.bypassed)
            return true;
        for(int s = 0; s < PedalState::SLOT_LAST; s++)
            if(slot_bypassed[s] != o.slot_bypassed[s]
               || slot_effect[s] != o.slot_effect[s])
                return true;
        for(int p = 0; p < PedalState::PAGE_LAST; p++)
            for(int k = 0; k < PedalState::kKnobCount; k++)
            {
                const float d = param[p][k] - o.param[p][k];
                if(d > kEpsilon || d < -kEpsilon)
                    return true;
            }
        return false;
    }

    bool operator==(const SavedState& o) const { return !(*this != o); }
};

/** The whole preset bank, which is what actually lives in flash.
 *
 *  One block rather than sixteen: PersistentStorage keeps a single struct at a
 *  single address, and the flash erases a sector at a time anyway, so writing
 *  one preset costs exactly what writing all of them costs.
 *
 *  There is no separate "save" gesture. The live settings are written back into
 *  whichever preset is current, so a preset is a working slot rather than a
 *  snapshot you have to remember to commit -- on a pedal with no screen, a save
 *  step you can forget is a save step that loses your sound. Recalling another
 *  preset saves the one you are leaving first.
 */
struct SavedBank
{
    static constexpr uint32_t kVersion    = 3;
    static constexpr int      kPresetCount = 16; /**< program change 0-15 */

    uint32_t   version;
    int32_t    current;
    SavedState preset[kPresetCount];

    bool operator!=(const SavedBank& o) const
    {
        if(version != o.version || current != o.current)
            return true;
        for(int i = 0; i < kPresetCount; i++)
            if(preset[i] != o.preset[i])
                return true;
        return false;
    }

    bool operator==(const SavedBank& o) const { return !(*this != o); }
};

/** Snapshot the settings out of a model. */
inline SavedState CaptureState(const PedalState& state)
{
    SavedState s{};
    s.version = SavedState::kVersion;
    for(int p = 0; p < PedalState::PAGE_LAST; p++)
        for(int k = 0; k < PedalState::kKnobCount; k++)
            s.param[p][k]
                = state.GetKnob(static_cast<PedalState::Page>(p), k);
    s.page  = static_cast<int32_t>(state.GetPage());
    s.order = static_cast<int32_t>(state.GetOrder());
    for(int i = 0; i < PedalState::SLOT_LAST; i++)
    {
        const PedalState::Slot slot = static_cast<PedalState::Slot>(i);
        s.slot_bypassed[i] = state.SlotBypassed(slot) ? 1 : 0;
        s.slot_effect[i]   = static_cast<uint8_t>(state.GetSlotEffect(slot));
    }
    s.bypassed = state.IsBypassed() ? 1 : 0;
    return s;
}

/** Push saved settings into a model. Returns false, leaving the model alone, if
 *  the block is not one we understand or its contents are out of range --
 *  flash that has never been written reads as whatever was left in it. */
inline bool ApplyState(const SavedState& s, PedalState* state)
{
    if(s.version != SavedState::kVersion)
        return false;
    if(s.page < 0 || s.page >= PedalState::PAGE_LAST)
        return false;
    if(s.order < 0 || s.order >= PedalState::kOrderCount)
        return false;

    for(int p = 0; p < PedalState::PAGE_LAST; p++)
        for(int k = 0; k < PedalState::kKnobCount; k++)
        {
            const float v = s.param[p][k];
            // Rejects NaN as well as out-of-range: every comparison with NaN is
            // false, so the negated test catches it.
            if(!(v >= 0.f && v <= 1.f))
                return false;
        }

    for(int p = 0; p < PedalState::PAGE_LAST; p++)
        for(int k = 0; k < PedalState::kKnobCount; k++)
            state->SetKnob(static_cast<PedalState::Page>(p), k, s.param[p][k]);

    state->SetPage(static_cast<PedalState::Page>(s.page));
    state->SetOrder(s.order);
    for(int i = 0; i < PedalState::SLOT_LAST; i++)
    {
        const PedalState::Slot slot = static_cast<PedalState::Slot>(i);
        state->SetSlotBypass(slot, s.slot_bypassed[i] != 0);

        // Not range-checked against the registry here: SavedState is
        // deliberately free of it so the round trip stays host-testable. An id
        // this build does not have resolves to a real effect at lookup time
        // rather than leaving a hole in the audio path.
        state->SetSlotEffect(slot, static_cast<int>(s.slot_effect[i]));
    }
    state->SetBypass(s.bypassed != 0);
    return true;
}

#endif
