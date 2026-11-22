#pragma once
#ifndef BOONTA_RNBO_PATCH_H
#define BOONTA_RNBO_PATCH_H

/** Pulls one exported RNBO patch into a namespace of its own.
 *
 *  Define BOONTA_RNBO_PATCH_NAME and BOONTA_RNBO_PATCH_FILE, then include this;
 *  afterwards `BoontaPatcher` is that patch's patcher type. One patch per
 *  translation unit, which is what makes three of them possible.
 *
 *  ## Why the namespace
 *
 *  Every RNBO export declares the same things: `RNBO::rnbomatic`, and the two
 *  entry points `GetPatcherFactoryFunction` and `SetLogger`. Three exports in
 *  one program therefore collide three ways, and only one of the three is
 *  solved by keeping each in its own file. Wrapping each export in a namespace
 *  separates the patcher classes; the two entry points are `extern "C"`, so a
 *  namespace cannot touch them and they are renamed by macro instead. Nothing
 *  here calls either -- the patcher is constructed directly -- so renaming them
 *  costs nothing.
 *
 *  ## Why the library comes first
 *
 *  RNBO_Common.h and RNBO_AudioSignal.h are included at global scope *before*
 *  the namespace opens, so the RNBO library itself -- Float64Buffer, the
 *  engine, the logger -- lands in the real `::RNBO` once and is shared by all
 *  three patches. The export's own copies of those includes then hit their
 *  include guards and do nothing. This is not only tidier: those classes have
 *  vague linkage, so the linker folds the three copies into one, and the three
 *  patches cost far less together than three times one. The three exports carry
 *  byte-identical `rnbo/` trees, so there is one definition to fold.
 *
 *  The `using namespace ::RNBO` is what lets the wrapped export still find the
 *  library: name lookup from inside `<patch>::RNBO` walks out to `<patch>` and
 *  then to the global namespace, and never into `::RNBO` on its own.
 */

#if !defined(BOONTA_RNBO_PATCH_NAME) || !defined(BOONTA_RNBO_PATCH_FILE)
#error \
    "define BOONTA_RNBO_PATCH_NAME and BOONTA_RNBO_PATCH_FILE before including RnboPatch.h"
#endif

#include "RNBO_Common.h"
#include "RNBO_AudioSignal.h"

#define BOONTA_RNBO_STR_IMPL(x) #x
#define BOONTA_RNBO_STR(x) BOONTA_RNBO_STR_IMPL(x)
#define BOONTA_RNBO_CAT_IMPL(a, b) a##b
#define BOONTA_RNBO_CAT(a, b) BOONTA_RNBO_CAT_IMPL(a, b)

#define GetPatcherFactoryFunction \
    BOONTA_RNBO_CAT(boonta_unused_factory_, BOONTA_RNBO_PATCH_NAME)
#define SetLogger \
    BOONTA_RNBO_CAT(boonta_unused_setlogger_, BOONTA_RNBO_PATCH_NAME)

namespace BOONTA_RNBO_PATCH_NAME
{
using namespace ::RNBO;
#include BOONTA_RNBO_STR(BOONTA_RNBO_PATCH_FILE)
} // namespace BOONTA_RNBO_PATCH_NAME

#undef GetPatcherFactoryFunction
#undef SetLogger

/** This translation unit's patcher. */
using BoontaPatcher = BOONTA_RNBO_PATCH_NAME::RNBO::rnbomatic<>;

#endif
