#pragma once
#ifndef BOONTA_MIDI_MAP_H
#define BOONTA_MIDI_MAP_H

#include <stdint.h>

#include "PedalState.h"

/** Which continuous controller does what.
 *
 *  Every number here is in a range the MIDI specification leaves undefined
 *  -- 20-31 and 102-119 -- so nothing collides with bank select, modulation,
 *  volume, the pedal controllers or the channel mode messages.
 *
 *  | CC        | Controls                                              |
 *  |-----------|-------------------------------------------------------|
 *  | 102 - 107 | Pitch, Geometry, Brightness, Damping, Position, Space |
 *  | 108 - 113 | Contour, Bow, Blow, Strike, Input, Strength           |
 *  | 114 - 119 | Bow timbre, Flow, Blow timbre, Mallet, Strike timbre, |
 *  |           | Fine                                                  |
 *  | 20        | Model, scaled across the three positions              |
 *  | 21        | Trigger, scaled across the three positions            |
 *  | 22        | Input routing, scaled across the three positions      |
 *  | 23        | Knob page, scaled across the three                    |
 *  | 30        | Gate         (>=64 held)                              |
 *  | 31        | Bypass       (>=64 in circuit)                        |
 *
 *  Notes play it: note-on sets the pitch (as a transposition around middle
 *  C, so the pitch knob still means something) and holds the gate, velocity
 *  scales Strength, and pitch bend is +/- two semitones. Program Change 0-2
 *  selects the knob page. Any channel mode message (CC 120-127, which is
 *  where All Notes Off lives) releases every held note, so a stuck note has
 *  a way out that is not the power switch.
 */
namespace midimap
{
/** Respond on every channel. Set to 0-15 to listen to one. */
static constexpr int kOmniChannel = -1;
static constexpr int kChannel     = kOmniChannel;

static constexpr uint8_t kParamBase = 102; /**< .. 119, in Param order */

static constexpr uint8_t kModelCc   = 20;
static constexpr uint8_t kTriggerCc = 21;
static constexpr uint8_t kRouteCc   = 22;
static constexpr uint8_t kPageCc    = 23;
static constexpr uint8_t kGateCc    = 30;
static constexpr uint8_t kBypassCc  = 31;

static_assert(PedalState::PARAM_LAST == 18, "the parameter block is 102-119");

/** A switch CC counts as "on" from half travel up, the usual convention. */
static constexpr uint8_t kSwitchThreshold = 64;

/** Pitch bend range, in semitones either way. */
static constexpr float kBendRange = 2.f;

/** What a controller number addresses. */
enum class Target
{
    NONE,
    PARAM,  /**< index is the Param */
    TOGGLE, /**< index is the Toggle */
    PAGE,
    GATE,
    BYPASS,
};

struct Binding
{
    Target target = Target::NONE;
    int    index  = 0;
};

/** Decode a controller number. Unmapped controllers return Target::NONE,
 *  which is most of them. */
inline Binding Decode(uint8_t cc)
{
    Binding b;

    if(cc >= kParamBase && cc < kParamBase + PedalState::PARAM_LAST)
    {
        b.target = Target::PARAM;
        b.index  = cc - kParamBase;
        return b;
    }

    switch(cc)
    {
        case kModelCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_MODEL;
            break;
        case kTriggerCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_TRIGGER;
            break;
        case kRouteCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_ROUTE;
            break;
        case kPageCc: b.target = Target::PAGE; break;
        case kGateCc: b.target = Target::GATE; break;
        case kBypassCc: b.target = Target::BYPASS; break;
        default: break;
    }

    return b;
}

/** 0-127 to 0-1, with 127 landing exactly on 1.0. */
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
