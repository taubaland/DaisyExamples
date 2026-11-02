#pragma once
#include <cstdint>

// The contract between this target-side writer and the openocd script that
// drives it. Nothing here is Daisy-specific; it is just the shape of the
// mailbox both sides read and write.
//
// The host finds the mailbox by symbol -- qspi-write.sh pulls the address of
// g_mailbox out of the ELF with `nm` -- so none of these offsets are hardcoded
// anywhere. Change the struct and both sides follow.
namespace qspiwriter
{
// One chunk of payload per round trip. 32K keeps the buffer comfortably inside
// DTCMRAM (128K, shared with the stack) and costs about ten round trips for a
// typical app image, which is nothing next to the erase.
constexpr uint32_t kChunkBytes = 32 * 1024;

// Written by the host *after* the rest of the command is in place, and cleared
// by the writer as soon as it picks the command up. Without it a resume that
// beat the host's own writes would run a half-written command.
constexpr uint32_t kMagicReq = 0x51575231; // "QWR1"

enum Command : uint32_t
{
    kCmdErase = 1, // erase [address, address + size)
    kCmdWrite = 2, // write size bytes of data[] at address, then verify them
    kCmdDone  = 3, // nothing left to do; park and blink
};

enum Status : uint32_t
{
    kStatusReady      = 1, // waiting for a command
    kStatusBusy       = 2, // one in progress
    kStatusOk         = 3, // the last one succeeded
    kStatusErrQspi    = 4, // the QSPI driver refused; detail = QSPIHandle::Status
    kStatusErrVerify  = 5, // readback differed; detail = offset of the first byte
    kStatusErrCommand = 6, // unknown command; detail = the command
    kStatusErrRange   = 7, // address or size out of bounds; detail = the size
};

// The IS25LP064A on the Seed: 8MB, mapped at 0x90000000.
constexpr uint32_t kQspiBase = 0x90000000;
constexpr uint32_t kQspiSize = 8 * 1024 * 1024;

struct Mailbox
{
    volatile uint32_t magic;
    volatile uint32_t command;
    volatile uint32_t address;
    volatile uint32_t size;
    volatile uint32_t status;
    volatile uint32_t detail;
    uint32_t          reserved[2]; // keeps data[] 32-byte aligned
    uint8_t           data[kChunkBytes];
};
} // namespace qspiwriter
