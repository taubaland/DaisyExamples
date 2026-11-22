#pragma once
#ifndef BOONTA_MIDI_MAP_H
#define BOONTA_MIDI_MAP_H

#include <stdint.h>

#include "PedalState.h"

/** Which continuous controller does what.
 *
 *  Every number here is in 20-31, a range the MIDI specification leaves
 *  undefined, so nothing collides with bank select, modulation, volume, the
 *  pedal controllers or the channel mode messages. Notably *not* 32-63: those
 *  are the LSBs of controllers 0-31, and although most gear ignores them, a
 *  controller sending 14-bit CCs would move two parameters at once.
 *
 *  Every parameter and every switch on the pedal is here -- there is nothing
 *  you can reach with a hand that you cannot reach with a controller. The
 *  numbers line up with the Clouds build's wherever the two pedals have the
 *  same kind of control, so one controller template drives both.
 *
 *  | CC      | Controls                                     |
 *  |---------|----------------------------------------------|
 *  | 20 - 25 | Time, Size, Pitch, Density, Shape, Mix       |
 *  | 26      | Seed, scaled across the three positions      |
 *  | 27      | Feedback, scaled across the three positions  |
 *  | 28      | Quality, scaled across the three positions   |
 *  | 29      | Pitch quantisation, scaled across the three  |
 *  | 30      | Freeze  (>=64 frozen)                        |
 *  | 31      | Bypass  (>=64 in circuit)                    |
 *
 *  Program Change 0-2 also selects the quantisation, which is what program
 *  change is for and what a footswitch controller will send.
 */
namespace midimap
{
/** Respond on every channel. Set to 0-15 to listen to one. */
static constexpr int kOmniChannel = -1;
static constexpr int kChannel     = kOmniChannel;

static constexpr uint8_t kParamBase = 20; /**< .. 25, in Param order */

static constexpr uint8_t kSeedCc     = 26;
static constexpr uint8_t kFeedbackCc = 27;
static constexpr uint8_t kQualityCc  = 28;
static constexpr uint8_t kQuantiseCc = 29;
static constexpr uint8_t kFreezeCc   = 30;
static constexpr uint8_t kBypassCc   = 31;

static_assert(PedalState::PARAM_LAST == 6, "the parameter block is CC 20-25");

/** A switch CC counts as "on" from half travel up, the usual convention. */
static constexpr uint8_t kSwitchThreshold = 64;

/** What a controller number addresses. */
enum class Target
{
    NONE,
    PARAM,  /**< index is the Param */
    TOGGLE, /**< index is the Toggle */
    QUANTISE,
    FREEZE,
    BYPASS,
};

struct Binding
{
    Target target = Target::NONE;
    int    index  = 0;
};

/** Decode a controller number. Unmapped controllers return Target::NONE,
 *  which is most of them -- a pedal that lurched every time a controller sent
 *  modulation would be unusable. */
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
        case kSeedCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_SEED;
            break;
        case kFeedbackCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_FEEDBACK;
            break;
        case kQualityCc:
            b.target = Target::TOGGLE;
            b.index  = PedalState::TOGGLE_QUALITY;
            break;
        case kQuantiseCc: b.target = Target::QUANTISE; break;
        case kFreezeCc: b.target = Target::FREEZE; break;
        case kBypassCc: b.target = Target::BYPASS; break;
        default: break;
    }

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
