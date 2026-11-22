#include "EffectRegistry.h"

#include "DriveEffect.h"
#include "EqEffect.h"
#include "ReverbEffect.h"
#include "RnboEffects.h"

namespace
{
EqEffect     eq;
DriveEffect  drive;
ReverbEffect reverb;

/** Indexed by effects::Id, so the order of this array *is* the id.
 *
 *  The RNBO entries are function calls rather than addresses because each patch
 *  lives in a translation unit of its own -- one export per unit is what lets
 *  three of them coexist, so this file cannot see their types. Each returns a
 *  file-static, so there is nothing here that can fail or come back null. */
Effect* const kEffects[effects::ID_LAST] = {
    &eq,     // ID_EQ
    &drive,  // ID_DRIVE
    &reverb, // ID_REVERB
#ifdef BOONTA_RNBO
    RnboPitchshifter(), // ID_PITCH
    RnboOctaver(),      // ID_OCTAVER
    RnboFreezer(),      // ID_FREEZER
#endif
};
} // namespace

int effects::Count()
{
    return ID_LAST;
}

bool effects::Known(int id)
{
    return id >= 0 && id < ID_LAST;
}

Effect* effects::Get(int id)
{
    // Falling back rather than returning null: a slot with no effect in it
    // would be a hole in the audio path, and the caller would have to check on
    // every block. A preset from a newer firmware naming an effect this build
    // does not have is the realistic way to get here.
    return Known(id) ? kEffects[id] : kEffects[0];
}

void effects::InitAll(float sample_rate)
{
    for(int i = 0; i < ID_LAST; i++)
        kEffects[i]->Init(sample_rate);
}
