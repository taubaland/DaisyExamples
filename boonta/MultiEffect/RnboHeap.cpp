#ifdef BOONTA_RNBO

#include <stddef.h>
#include <string.h>

#include "dev/sdram.h" // DSY_SDRAM_BSS
#include "RnboHeap.h"
#include "RNBO_Platform.h" // the four functions RNBO_USECUSTOMALLOCATOR leaves to us

/** Where an RNBO patch's buffers live.
 *
 *  RNBO sizes a `delay~` from the sample rate and rounds up to a power of two,
 *  and a data ref is a Float64Buffer whatever RNBO_USE_FLOAT32 says about
 *  SampleValue -- that flag governs the signal path, not the buffers. The pitch
 *  shifter has four delay lines of one second each, so it asks for
 *  65536 * 8 bytes four times over: 2 MB.
 *
 *  The firmware's heap is whatever DTCMRAM is left after .bss, which in the
 *  RNBO build is about 10 KB shared with the stack. patcher.initialize() walked
 *  off the end of it and hard-faulted inside calloc's memset -- on hardware,
 *  at boot, exactly as RnboEffect.h warned it would. There is nowhere in
 *  internal memory for 2 MB: SRAM is 480 KB and the app already holds 43% of
 *  it. The Seed's 64 MB of SDRAM is the only candidate, and it is already where
 *  the reverb keeps its tank.
 *
 *  So RNBO gets an arena of its own and the rest of the firmware keeps the
 *  DTCMRAM heap it has always had. This is deliberately not a global
 *  replacement of malloc: nothing else here allocates, and a change that
 *  silently moved libDaisy's and FatFs's allocations to external memory would
 *  be a much larger claim than this one.
 *
 *  **This costs speed, and that is the point of measuring.** SDRAM is external:
 *  the reverb's tank traffic is the standing suspect for most of the 26 points
 *  that effect costs. A pitch shifter reading four interpolated delay lines out
 *  of SDRAM will pay the same kind of toll. There is no faster placement
 *  available, so the number measured with this in place *is* the number.
 *
 *  Measured, with the pitch shifter: 2118144 bytes taken, no allocation ever
 *  refused, and the high-water mark equal to the total because nothing is ever
 *  handed back. The arena is sized well above that so a second patch, or a
 *  bigger one, does not repeat the fault this file exists to fix; trim it if
 *  SDRAM is ever wanted for something else.
 *
 *  A bump allocator, because the pattern it serves is a handful of buffers
 *  claimed once inside Init() and held until power-off. free() rewinds only the
 *  most recent block, which is enough for RNBO's grow-copy-release path and
 *  leaks the old buffer otherwise -- acceptable against an arena sized well
 *  above the high-water mark, and honest about what it is. Nothing here may be
 *  called from the audio thread; RnboEffect::Process does not allocate.
 */
namespace
{
constexpr size_t kArenaSize = 8 * 1024 * 1024;
constexpr size_t kAlign     = 16;

/** Not zeroed by the startup code -- SDRAM never is. Everything handed out
 *  goes through calloc or is written before it is read. */
alignas(kAlign) char arena[kArenaSize] DSY_SDRAM_BSS;

inline size_t RoundUp(size_t n)
{
    return (n + kAlign - 1) & ~(kAlign - 1);
}
} // namespace

/** Arena accounting, for a debugger to read: how much the patch actually took,
 *  and whether it ever came up short. In DTCMRAM .bss, so zeroed at startup.
 *  extern "C" and unmangled so `print rnbo_heap_high_water` works in gdb. */
extern "C"
{
    volatile size_t rnbo_heap_used;
    volatile size_t rnbo_heap_high_water;
    volatile size_t rnbo_heap_failed;
}

namespace RNBO
{
namespace Platform
{
    void* malloc(size_t size)
    {
        // A header before every block, so free() and realloc() know how big it was.
        const size_t need = RoundUp(size) + kAlign;
        const size_t at
            = rnbo_heap_used; // one read of the volatile, then arithmetic

        if(at + need > kArenaSize)
        {
            rnbo_heap_failed++;
            return nullptr;
        }

        char* header                       = arena + at;
        *reinterpret_cast<size_t*>(header) = size;

        rnbo_heap_used = at + need;
        if(at + need > rnbo_heap_high_water)
            rnbo_heap_high_water = at + need;

        return header + kAlign;
    }

    void free(void* ptr)
    {
        if(!ptr)
            return;

        char*        block = static_cast<char*>(ptr);
        const size_t size  = *reinterpret_cast<size_t*>(block - kAlign);

        // Only the most recent block can be given back. Anything else stays where
        // it is; see the note above about why that is enough here.
        if(block + RoundUp(size) == arena + rnbo_heap_used)
            rnbo_heap_used = static_cast<size_t>(block - kAlign - arena);
    }

    void* calloc(size_t count, size_t size)
    {
        const size_t total = count * size;
        void*        block = Platform::malloc(total);

        if(block)
            memset(block, 0, total);

        return block;
    }

    void* realloc(void* ptr, size_t size)
    {
        if(!ptr)
            return Platform::malloc(size);

        const size_t old
            = *reinterpret_cast<size_t*>(static_cast<char*>(ptr) - kAlign);
        void* block = Platform::malloc(size);

        if(block)
            memcpy(block, ptr, old < size ? old : size);

        Platform::free(ptr);
        return block;
    }

} // namespace Platform
} // namespace RNBO

/** Where the patcher objects live.
 *
 *  A patcher is not just code: it is the struct holding every delay index,
 *  filter state and phasor the patch runs on, and it is touched constantly.
 *  The three here come to 38982 bytes together, and DTCMRAM -- where a static
 *  object would land -- has about ten thousand left after .bss, with the stack
 *  living in the same space. SDRAM would fit but is external, and putting the
 *  hot state there would cost more than the delay lines do.
 *
 *  RAM_D2 is 256 KB of internal SRAM that this firmware does not otherwise
 *  touch, and the linker script already reaches it: `.heap` is the only section
 *  mapped there, and `end` -- where malloc starts -- is the address just after
 *  it. So a pool placed in `.heap` sits at the front of RAM_D2 and pushes the
 *  malloc heap along behind it. No linker script change, and no chance of the
 *  two colliding.
 *
 *  That last point matters more than it looks. The `_sbrk` this build links has
 *  no bounds check at all -- it hands out whatever is asked for and moves the
 *  break, so an allocation past the end of RAM_D2 succeeds and faults later, on
 *  first touch, somewhere else entirely. That is exactly how the pitch
 *  shifter's first 512 KB delay line brought the pedal down. Nothing here can
 *  repeat it: this pool is bounded and refuses.
 */
namespace
{
/** Sized for the three patches with room to spare: pitch shifter 8226, octaver
 *  7986, freezer 22770. RAM_D2 keeps the rest for malloc, which after the
 *  arena above is barely used. */
constexpr size_t kPatcherPoolSize = 64 * 1024;

char patcher_pool[kPatcherPoolSize]
    __attribute__((section(".heap"), aligned(8)));

size_t patcher_pool_used_bytes;
} // namespace

/** How much of the patcher pool went, for a debugger to read. */
extern "C"
{
    volatile size_t rnbo_patcher_used;
    volatile size_t rnbo_patcher_failed;
}

void* rnbo_heap::AllocatePatcher(size_t size, size_t align)
{
    const size_t at = (patcher_pool_used_bytes + align - 1) & ~(align - 1);

    if(at + size > kPatcherPoolSize)
    {
        rnbo_patcher_failed++;
        return nullptr;
    }

    patcher_pool_used_bytes = at + size;
    rnbo_patcher_used       = patcher_pool_used_bytes;

    return patcher_pool + at;
}

#endif // BOONTA_RNBO
