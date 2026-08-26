#pragma once
#ifndef BOONTA_MIDI_MAP_H
#define BOONTA_MIDI_MAP_H

#include <stdint.h>

#include "PedalState.h"

/** Which continuous controller does what.
 *
 *  Every number here is in a range the MIDI specification leaves undefined --
 *  20-31 and 102-119 -- so nothing collides with bank select, modulation,
 *  volume, the pedal controllers or the channel mode messages. Notably *not*
 *  32-63: those are the LSBs of controllers 0-31, and although most gear
 *  ignores them, a controller sending 14-bit CCs would move two parameters at
 *  once.
 *
 *  Thirty controls: twenty-four page parameters, then the switches.
 *
 *  | CC        | Controls                                     |
 *  |-----------|----------------------------------------------|
 *  | 20 - 25   | EQ page, knobs 1-6                           |
 *  | 26 - 31   | Drive page, knobs 1-6                        |
 *  | 102 - 107 | Reverb page, knobs 1-6                       |
 *  | 108 - 113 | Meta page, knobs 1-6                         |
 *  | 114       | Master bypass  (>=64 in circuit)             |
 *  | 115 - 117 | EQ / drive / reverb switched in (>=64 in)    |
 *  | 118       | Chain order, scaled across the six           |
 *  | 119       | Page select, scaled across the four          |
 *
 *  Presets are recalled with Program Change rather than a controller, which is
 *  what program change is for.
 */
namespace midimap
{
/** Respond on every channel. Set to 0-15 to listen to one. */
static constexpr int kOmniChannel = -1;
static constexpr int kChannel     = kOmniChannel;

static constexpr uint8_t kEqBase     = 20;  /**< .. 25 */
static constexpr uint8_t kDriveBase  = 26;  /**< .. 31 */
static constexpr uint8_t kReverbBase = 102; /**< .. 107 */
static constexpr uint8_t kMetaBase   = 108; /**< .. 113 */

static constexpr uint8_t kMasterBypass = 114;
static constexpr uint8_t kSlotBase     = 115; /**< .. 117, in Slot order */
static constexpr uint8_t kChainOrder   = 118;
static constexpr uint8_t kPageSelect   = 119;

/** A switch CC counts as "on" from half travel up, the usual convention. */
static constexpr uint8_t kSwitchThreshold = 64;

/** What a controller number addresses. */
enum class Target
{
    NONE,
    PARAM,         /**< page + knob   */
    MASTER_BYPASS,
    SLOT_BYPASS,   /**< index is the slot */
    CHAIN_ORDER,
    PAGE_SELECT,
};

struct Binding
{
    Target target = Target::NONE;
    int    page   = 0; /**< PARAM only */
    int    knob   = 0; /**< PARAM only */
    int    slot   = 0; /**< SLOT_BYPASS only */
};

/** Decode a controller number. Unmapped controllers return Target::NONE, which
 *  is most of them -- a pedal that lurched every time a controller sent
 *  modulation would be unusable. */
inline Binding Decode(uint8_t cc)
{
    Binding b;

    struct PageBlock
    {
        uint8_t base;
        int     page;
    };
    static constexpr PageBlock kBlocks[] = {
        {kEqBase, PedalState::PAGE_EQ},
        {kDriveBase, PedalState::PAGE_DRIVE},
        {kReverbBase, PedalState::PAGE_REVERB},
        {kMetaBase, PedalState::PAGE_META},
    };

    for(const auto& blk : kBlocks)
    {
        if(cc >= blk.base && cc < blk.base + PedalState::kKnobCount)
        {
            b.target = Target::PARAM;
            b.page   = blk.page;
            b.knob   = cc - blk.base;
            return b;
        }
    }

    if(cc == kMasterBypass)
        b.target = Target::MASTER_BYPASS;
    else if(cc >= kSlotBase && cc < kSlotBase + PedalState::SLOT_LAST)
    {
        b.target = Target::SLOT_BYPASS;
        b.slot   = cc - kSlotBase;
    }
    else if(cc == kChainOrder)
        b.target = Target::CHAIN_ORDER;
    else if(cc == kPageSelect)
        b.target = Target::PAGE_SELECT;

    return b;
}

/** 0-127 to 0-1. 127 must land exactly on 1.0, or a controller at full travel
 *  leaves a mix knob a hair short of fully wet. */
inline float Normalise(uint8_t value)
{
    return static_cast<float>(value) / 127.f;
}

/** 0-127 spread across n slots, with the top value landing on the last one. */
inline int Quantise(uint8_t value, int n)
{
    int i = (static_cast<int>(value) * n) / 128;
    if(i >= n)
        i = n - 1;
    return i;
}

inline bool IsOn(uint8_t value)
{
    return value >= kSwitchThreshold;
}

} // namespace midimap

#endif
