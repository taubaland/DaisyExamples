#pragma once
#ifndef BOONTA_RNBO_EFFECTS_H
#define BOONTA_RNBO_EFFECTS_H

/** The RNBO patches this build has, for the registry.
 *
 *  Deliberately thin: none of RNBO appears here, so EffectRegistry.cpp does not
 *  pay for it. Each of these returns a pointer to a file-static effect in its
 *  own translation unit -- one patch per unit, which is what lets three
 *  exports coexist. See RnboPatch.h.
 */

#ifdef BOONTA_RNBO

class Effect;

Effect* RnboPitchshifter();
Effect* RnboOctaver();
Effect* RnboFreezer();

#endif
#endif
