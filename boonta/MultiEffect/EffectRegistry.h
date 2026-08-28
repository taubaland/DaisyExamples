#pragma once
#ifndef BOONTA_EFFECT_REGISTRY_H
#define BOONTA_EFFECT_REGISTRY_H

#include "Effect.h"

/** Every effect the firmware can put in a slot.
 *
 *  Instances are static and all resident at once, which is the right trade on a
 *  device with 480K of SRAM and no allocator: an effect's state costs a few
 *  hundred bytes (the reverb's half-megabyte tank is in SDRAM), and having them
 *  all live means changing a slot is a pointer write rather than a
 *  construction. Nothing is allocated, and nothing can fail at the moment you
 *  turn a knob.
 *
 *  **Ids are persistent.** They are written into saved presets, so a preset
 *  recalled after a firmware update must find the same effect in the same slot.
 *  Add to the end of the list; never renumber, and never reuse the id of an
 *  effect you remove -- retire it instead, or old presets will silently come
 *  back as something else.
 */
namespace effects
{
/** Stable identifiers. Append only. */
enum Id
{
    ID_EQ     = 0,
    ID_DRIVE  = 1,
    ID_REVERB = 2,
    ID_LAST,
};

/** How many effects are actually available to put in a slot. */
int Count();

/** The effect for an id, or the first effect if the id is not one we know --
 *  which is what a preset written by a newer firmware looks like. Never null,
 *  because a null here would be a silent hole in the audio path. */
Effect* Get(int id);

/** True if this id names an effect this build has. */
bool Known(int id);

/** Initialise every registered effect. */
void InitAll(float sample_rate);

} // namespace effects

#endif
