// Stand-in for upstream's elements/drivers/debug_pin.h.
//
// NOT part of Mutable Instruments' sources. The original toggles a GPIO on
// the module's STM32F4 for profiling, and dsp/resonator.cc includes it
// without using it. This empty file is what lets resonator.cc compile here
// unmodified.

#ifndef ELEMENTS_DRIVERS_DEBUG_PIN_H_
#define ELEMENTS_DRIVERS_DEBUG_PIN_H_

#endif // ELEMENTS_DRIVERS_DEBUG_PIN_H_
