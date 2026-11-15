// Upstream's DSP, compiled as one translation unit.
//
// libDaisy's Makefile only knows `.cpp`, and upstream's files are `.cc`.
// Rather than rename them -- which would make every future diff against
// pichenettes/eurorack a rename plus a change -- they are included from here
// under the names they were written with.
//
// The `-I.` on the command line is what makes `elements/...` resolve to the
// vendored copy, and it is also what makes resonator.cc's one piece of
// STM32F4 hardware, `elements/drivers/debug_pin.h`, resolve to the empty
// stand-in beside it.

#include "elements/dsp/exciter.cc"
#include "elements/dsp/multistage_envelope.cc"
#include "elements/dsp/ominous_voice.cc"
#include "elements/dsp/part.cc"
#include "elements/dsp/resonator.cc"
#include "elements/dsp/string.cc"
#include "elements/dsp/tube.cc"
#include "elements/dsp/voice.cc"

// The three pieces of stmlib the above link against. stmlib is a submodule
// at the top of this repository.
#include "stmlib/dsp/atan.cc"
#include "stmlib/dsp/units.cc"
#include "stmlib/utils/random.cc"
