#pragma once
#ifndef BOONTA_SAMPLES_H
#define BOONTA_SAMPLES_H

#include <stdint.h>

#include "daisy_boonta.h"

/** Elements' sample data, and why it is not in the program.
 *
 *  The strike and blow exciters play back recordings: 338 KB of 16-bit audio
 *  in two tables, `smp_sample_data` and `smp_noise_sample`. The Daisy
 *  bootloader will copy at most 480 KB into RAM, the rest of this firmware
 *  is about 150 KB, and running the program in place from QSPI instead would
 *  give up both the speed and the ability to write settings to that same
 *  chip. So the two tables are taken out of the program:
 *
 *  - `ElementsResources.cpp` renames them as it compiles upstream's
 *    `resources.cc`, so that nothing in the program refers to the copies in
 *    it and the linker drops them.
 *  - The Makefile cuts them out of that object file into `samples.bin`, with
 *    the header below, and appends it to the program image 512 KB in -- past
 *    anything the bootloader will copy. One file, one write.
 *  - The linker is told (`--defsym`) that the two tables live at fixed
 *    addresses at the top of SDRAM, which is where Load() puts them.
 *
 *  They are copied to SDRAM rather than read from flash where they lie
 *  because Persist takes the QSPI chip out of memory-mapped mode to save
 *  settings, for tens of milliseconds at a time, while the audio callback
 *  carries on reading samples.
 *
 *  Upstream's sources are untouched by any of this.
 */
namespace samples
{
/** Where the blob sits in the QSPI chip: 512 KB after the start of the
 *  application image at 0x40000. Must match SAMPLES_IMAGE_OFFSET in the
 *  Makefile. */
static constexpr uint32_t kFlashOffset = 0x40000 + 0x80000;

/** Where the noise table sits relative to the sample table, in the blob
 *  and in SDRAM. Must match SAMPLES_NOISE_OFFSET in the Makefile. */
static constexpr uint32_t kNoiseOffset = 0x40000;

struct Header
{
    uint32_t magic;    /**< kMagic */
    uint32_t version;  /**< kVersion */
    uint32_t size;     /**< payload bytes following the header */
    uint32_t checksum; /**< FNV-1a of the payload */
};

static constexpr uint32_t kMagic   = 0x4D534C45u; // 'ELSM'
static constexpr uint32_t kVersion = 1;

/** True once the tables are in SDRAM. A debugger that has loaded them there
 *  itself -- see `make sram-bare` -- sets this by hand, which is why it is a
 *  plain global and not a return value. */
extern volatile uint32_t loaded;

/** Find the blob, check it, and copy it into SDRAM. If it is missing or
 *  damaged the tables are zeroed instead, which leaves the sampled exciters
 *  silent and everything else working. Call after the board is up. */
void Load(daisy::QSPIHandle& qspi);

} // namespace samples

#endif
