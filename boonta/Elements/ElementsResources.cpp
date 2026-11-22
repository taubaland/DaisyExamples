// Upstream's lookup tables, minus the two that are too big to carry.
//
// resources.cc defines eighteen small lookup tables and two large sample
// tables. The small ones are wanted here; the large ones live in SDRAM,
// loaded from their own place in flash -- see Samples.h for why.
//
// Renaming the two as this file is compiled means the definitions that land
// in this object are not the ones the DSP refers to. Nothing refers to the
// renamed copies, so the linker's --gc-sections drops them from the program,
// and the Makefile lifts them out of this object file to build samples.bin.
// The names the DSP does refer to are given addresses on the link line.

#define smp_sample_data smp_sample_data_rom
#define smp_noise_sample smp_noise_sample_rom

#include "elements/resources.cc"
