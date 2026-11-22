// # QSPI writer
//
// A flash loader for the Boonta's QSPI chip, in the classic sense: a tiny
// program that runs *on the target* and does the writing, driven over the
// ST-Link by a debugger that has no idea how the flash works.
//
// ## Why this exists
//
// There are normally two ways to get an image into QSPI at 0x90040000, and
// this board has neither.
//
//   * The Daisy bootloader's DFU. It never enumerates here -- not the
//     intdfu variant, not the extdfu one. The USB itself is fine (the chip's
//     own ROM bootloader enumerates immediately on the same cable), but
//     libDaisy's USB stack does not come up on this board, and the same fault
//     sinks libDaisy's own seed/USB_MIDI example. See
//     ../NewEffectDuino/PORTING.md.
//
//   * openocd's stmqspi driver. It drops the SWD link the moment it probes,
//     with QUADSPI demonstrably clocked and configured; stmqspi is not usable
//     over ST-Link's HLA transport.
//
// The chip's ROM DFU does enumerate, but it only ever offers "@Internal Flash"
// and "@Option Bytes" -- there is no alt setting for QSPI, so it cannot help.
//
// What is left is the route every real flash loader takes: put a program in
// RAM that can reach the flash, and feed it the payload a chunk at a time.
// libDaisy's QSPIHandle already knows the IS25LP064A, so the program is mostly
// a mailbox and a loop.
//
// ## How it is driven
//
// qspi-write.sh loads this into AXI SRAM the same way `make sram` loads an
// app -- by catching the bootloader's handover with a hardware breakpoint, see
// qspi-write.cfg -- then drives it through QspiWriterReady(), a one-instruction
// function that exists only to be a breakpoint. Every time the writer is ready
// for work it calls that function, the host's breakpoint catches it, the host
// fills the mailbox and resumes. No polling of a running target, which ST-Link
// HLA is unreliable at; just halt, write, resume.
//
// ## Why it runs from SRAM
//
// You cannot write a flash chip you are executing from. QSPIHandle enforces
// this (CheckProgramMemory -> E_INVALID_MODE), and an APP_TYPE of BOOT_SRAM is
// what puts this program somewhere else. Do not be tempted to build it
// BOOT_QSPI.

#include "daisy_seed.h"

#include "QspiWriter.h"

using namespace daisy;
using namespace qspiwriter;

static DaisySeed hw;

// The startup code skips SystemInit() entirely for a BOOT_APP build, so VTOR
// is whatever the bootloader left it as -- the base of whichever image it
// thought it was starting. That is harmless when the resident QSPI image is a
// BOOT_SRAM one, because the address it points at is the same 0x24000000 this
// program is loaded over. It is not harmless once a BOOT_QSPI image is
// resident, which is exactly the situation this program creates: VTOR would
// point into QSPI, every interrupt would vector through a flash chip that is
// about to be erased, and -- sooner -- DaisySeed::Init() reads VTOR to decide
// where it is running from and would skip qspi.Init() as a result, leaving the
// writer with no QSPI driver at all.
extern "C" void* g_pfnVectors[];

// Deliberately a plain .bss object rather than anything at a fixed address:
// the host reads its address out of the ELF. For the BOOT_SRAM layout .bss is
// in DTCMRAM, which is both reachable by the debugger and off-limits to DMA --
// and QSPIHandle writes with a blocking HAL_QSPI_Transmit, so DTCMRAM is fine
// as a source buffer where it would not be for, say, the audio engine.
// extern "C" so that the symbol in the ELF is plainly `g_mailbox` and not a
// mangled, file-local one: qspi-write.sh finds the mailbox with `nm`, and a
// static would be both renamed and invisible to it.
extern "C"
{
    Mailbox g_mailbox;
}

// The host's breakpoint target. `used` and `noinline` because nothing calls it
// for its behaviour and there is nothing in it to keep -- without both, the
// symbol the host breaks on would not survive -O2.
extern "C" __attribute__((noinline, used)) void QspiWriterReady(void)
{
    __asm__ volatile("nop" ::: "memory");
}

namespace
{
void Fail(Status status, uint32_t detail)
{
    g_mailbox.detail = detail;
    g_mailbox.status = status;
}

// Both commands address the chip absolutely (0x90040000, not 0x40000), because
// that is what the bootloader, the linker scripts and the DFU route all use.
// QSPIHandle masks the address down itself.
bool InRange(uint32_t address, uint32_t size)
{
    if(address < kQspiBase || size > kChunkBytes)
        return false;
    const uint32_t offset = address - kQspiBase;
    return offset <= kQspiSize && size <= kQspiSize - offset;
}

void DoErase()
{
    const uint32_t address = g_mailbox.address;
    const uint32_t size    = g_mailbox.size;

    // The chunk limit is a write-side limit; an erase names a span, not a
    // buffer, so check it on its own terms.
    if(address < kQspiBase || size > kQspiSize
       || (address - kQspiBase) > kQspiSize - size)
    {
        Fail(kStatusErrRange, size);
        return;
    }

    if(hw.qspi.Erase(address, address + size) != QSPIHandle::Result::OK)
    {
        Fail(kStatusErrQspi, hw.qspi.GetStatus());
        return;
    }

    g_mailbox.detail = 0;
    g_mailbox.status = kStatusOk;
}

void DoWrite()
{
    const uint32_t address = g_mailbox.address;
    const uint32_t size    = g_mailbox.size;

    if(!InRange(address, size))
    {
        Fail(kStatusErrRange, size);
        return;
    }

    if(hw.qspi.Write(address, size, g_mailbox.data) != QSPIHandle::Result::OK)
    {
        Fail(kStatusErrQspi, hw.qspi.GetStatus());
        return;
    }

    // Verify here rather than in a pass of its own at the end. The chunk is
    // still in the buffer, so it costs one memcmp and no extra round trips --
    // and Write() has already put the peripheral back in memory-mapped mode,
    // which is what makes the chip readable through a pointer at all.
    const uint8_t* resident = reinterpret_cast<const uint8_t*>(address);
    for(uint32_t i = 0; i < size; i++)
    {
        if(resident[i] != g_mailbox.data[i])
        {
            Fail(kStatusErrVerify, i);
            return;
        }
    }

    g_mailbox.detail = 0;
    g_mailbox.status = kStatusOk;
}
} // namespace

int main(void)
{
    SCB->VTOR = reinterpret_cast<uint32_t>(g_pfnVectors);

    hw.Configure();
    hw.Init();

    // Nothing else is brought up: no audio, no ADC, no SDRAM. The less this
    // program owns, the less there is to go wrong underneath a flash write,
    // and DaisySeed::Init() has already done the one thing that matters --
    // qspi.Init(), in memory-mapped mode.

    bool failed = false;

    g_mailbox.status = kStatusReady;

    for(;;)
    {
        // Where the host's breakpoint catches us, and the only place status is
        // ever read. On the first pass it is the handshake -- the writer is up
        // and the QSPI is configured. After that it is "the last command
        // finished, here is its result, give me another". Nothing may
        // overwrite status between finishing a command and arriving here.
        QspiWriterReady();

        // Belt and braces. The host writes the magic last and resumes after,
        // so this should fall through immediately; it only matters if a resume
        // ever beats the mailbox.
        while(g_mailbox.magic != kMagicReq) {}
        g_mailbox.magic = 0;

        const uint32_t command = g_mailbox.command;
        g_mailbox.status       = kStatusBusy;

        switch(command)
        {
            case kCmdErase: DoErase(); break;
            case kCmdWrite: DoWrite(); break;
            case kCmdDone: g_mailbox.status = kStatusOk; break;
            default: Fail(kStatusErrCommand, command); break;
        }

        failed = failed || g_mailbox.status != kStatusOk;

        if(command == kCmdDone)
            break;
    }

    // One last stop so the host can read the final status before it lets go.
    QspiWriterReady();

    // After which the LED is the only thing left that can say anything: a slow
    // blink for a clean run, a fast one if any command failed. The image in
    // QSPI only runs on the next reset, so this is what the board shows until
    // someone power-cycles it.
    const uint32_t period = failed ? 128 : 512;
    for(;;)
    {
        hw.SetLed((System::GetNow() % period) < (period / 2));
    }
}
