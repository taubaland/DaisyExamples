#include "Samples.h"

#include <string.h>

#include "elements/resources.h"

namespace samples
{
volatile uint32_t loaded = 0;

/** Everything the two tables occupy, from the start of the first to the end
 *  of the second. */
static constexpr uint32_t kPayloadSize
    = kNoiseOffset + SMP_NOISE_SAMPLE_SIZE * sizeof(int16_t);

static_assert(SMP_SAMPLE_DATA_SIZE * sizeof(int16_t) <= kNoiseOffset,
              "the sample table has outgrown its slot");

static uint32_t Fnv1a(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for(uint32_t i = 0; i < size; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

void Load(daisy::QSPIHandle& qspi)
{
    // The tables are const to the DSP and to the compiler. Their addresses
    // are whatever the linker was told, which is a stretch of SDRAM.
    uint8_t* const dest = reinterpret_cast<uint8_t*>(
        const_cast<int16_t*>(elements::smp_sample_data));

    const Header* const header
        = static_cast<const Header*>(qspi.GetData(kFlashOffset));
    const uint8_t* const payload
        = reinterpret_cast<const uint8_t*>(header + 1);

    // Size before checksum: an erased chip reads 0xFFFFFFFF, and that is
    // not a length to go and hash.
    const bool ok = header->magic == kMagic && header->version == kVersion
                    && header->size == kPayloadSize
                    && Fnv1a(payload, kPayloadSize) == header->checksum;

    if(ok)
        memcpy(dest, payload, kPayloadSize);
    else
        memset(dest, 0, kPayloadSize);

    loaded = ok ? 1 : 0;
}

} // namespace samples
