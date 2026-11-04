// Bootloader support for the Arduino build.
//
// Nothing here is compiled into a plain BOOT_NONE build. It only exists when
// build.sh is asked for BOOT_SRAM or BOOT_QSPI, which define BOOT_APP and
// link with -Wl,--wrap=SystemInit.
//
// The problem it solves: the STM32 core's Reset_Handler calls SystemInit()
// before main(), and the core's SystemInit() does two things that are fatal to
// a bootloaded app.
//
//   1. It resets the whole RCC tree back to HSI and switches every PLL off.
//      Under BOOT_QSPI the code is executing in place out of QSPI flash, so
//      pulling the QSPI kernel clock out from under the CPU locks the part up
//      on the next instruction fetch.
//   2. It writes SCB->VTOR = FLASH_BANK1_BASE, pointing the vector table at
//      internal flash -- where the bootloader lives, not this app. The first
//      interrupt would then vector into the bootloader's handlers.
//
// libDaisy solves this by not calling SystemInit() at all (the -DBOOT_APP in
// core/Makefile guards the call in startup_stm32h750xx.c). The Arduino core's
// startup is a fixed .s file we cannot edit, so instead the call is redirected
// at link time to the stub below, which keeps the one part of SystemInit() we
// still want and drops the rest. The clocks are configured properly later by
// DAISY.init().

#ifdef BOOT_APP

#include <Arduino.h>

extern "C"
{
    /** Start of .isr_vector, placed by boot_sram.ld / boot_qspi.ld. Taking the
     *  address keeps the app's idea of where its vectors are in one place --
     *  the linker script -- rather than hard-coding 0x24000000 / 0x90040000
     *  here as well. */
    extern uint32_t _svector_table;

    void __wrap_SystemInit(void)
    {
        // The bootloader has already done this, but it costs two instructions
        // and makes the app independent of which bootloader version ran.
#if(__FPU_PRESENT == 1) && (__FPU_USED == 1)
        SCB->CPACR |= ((3UL << (10 * 2)) | (3UL << (11 * 2)));
#endif

        // Point the vector table at our own, wherever the linker put it.
        SCB->VTOR = reinterpret_cast<uint32_t>(&_svector_table);

        // Deliberately no RCC reset: the bootloader's clock configuration has
        // to survive until DAISY.init() replaces it.
    }
}

#endif // BOOT_APP
