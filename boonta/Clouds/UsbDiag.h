#pragma once
#ifndef BOONTA_USB_DIAG_H
#define BOONTA_USB_DIAG_H

#include <stdint.h>

/** Why USB does not enumerate, and the two things that might fix it.
 *
 *  The pedal attaches -- Windows sees a device appear -- and then never answers
 *  a descriptor request. Everything on the device side reads as configured:
 *  HSI48 on and ready, USBSEL = HSI48, the OTG_FS clock enabled, the
 *  transceiver powered, VBUS valid, the pull-up presented, NVIC and GINTMSK
 *  set. libDaisy's own `seed/USB_MIDI` example fails identically on the same
 *  board, cable and port, so it is not `MidiControl`.
 *
 *  Two candidates came out of reading libDaisy against ST's own material.
 *  Both are addressed from here rather than by forking libDaisy, because both
 *  can be: one is a peripheral libDaisy never turns on, the other is a bit
 *  libDaisy sets and never clears.
 *
 *  ## 1. HSI48 is never trimmed
 *
 *  `System::ConfigureClocks()` sets `UsbClockSelection = RCC_USBCLKSOURCE_HSI48`
 *  and stops there. Nothing in libDaisy touches the Clock Recovery System --
 *  `HAL_RCCEx_CRSConfig` appears nowhere in the tree -- so the 48 MHz the USB
 *  core runs on is a free-running RC oscillator with no reference.
 *
 *  A full-speed device has to transmit inside +/-0.25%. A free-running HSI48 is
 *  specified far wider than that, so whether a given board enumerates comes
 *  down to where its oscillator happens to sit and how tolerant the host is.
 *  That is exactly the shape of this fault: works for most people, fails
 *  completely on one board, and looks like nothing is wrong.
 *
 *  The asymmetry that makes this the leading candidate: the ROM DFU bootloader
 *  enumerates instantly on the same cable, port and pins. Per AN2606 the
 *  bootloader *enables CRS* so that USB can be clocked from HSI48. So the one
 *  USB stack that works on this board is the one that trims the oscillator,
 *  and the one that does not is libDaisy.
 *
 *  `EnableCrs()` does what the bootloader does: locks HSI48 to the host's
 *  start-of-frame, which arrives every millisecond from the moment the port is
 *  enabled -- before the first descriptor request.
 *
 *  ## 2. The PHY clock is gated on suspend and never ungated
 *
 *  `usbd_conf.c`'s suspend callback calls `__HAL_PCD_GATE_PHYCLOCK()`
 *  unconditionally; the resume callback does not ungate, and there is no wakeup
 *  handler. Hosts issue transient suspends routinely, including around driver
 *  attach. Once gated, the data path is dead in both directions until reset.
 *  Reported upstream as electro-smith/libDaisy#716, still open; the gating is
 *  not even wanted here, since `low_power_enable` is DISABLE.
 *
 *  `Service()` clears `PCGCCTL.STOPCLK` whenever it finds it set, which is the
 *  workaround that issue reports using.
 *
 *  ## Reading the result
 *
 *  Neither fix is confirmed on hardware -- that needs the board. `snapshot` is
 *  a global for the ST-Link to read while the host is trying to enumerate, in
 *  the same spirit as `PROBE_IO`. See the README for what the fields mean and
 *  which answer they give.
 */
namespace usbdiag
{
/** Registers, sampled in the main loop. Read over the ST-Link with
 *  `p/x usbdiag::snapshot` while the host is trying to enumerate. */
struct Snapshot
{
    uint32_t pcgcctl;  /**< bit 0 STOPCLK: the PHY clock is gated       */
    uint32_t gahbcfg;  /**< bit 0 GINT: interrupts reach the core at all */
    uint32_t gintsts;  /**< USBRST/ENUMDNE say a reset was seen         */
    uint32_t dsts;     /**< bit 0 SUSPSTS, bits 2:1 ENUMSPD             */
    uint32_t dctl;     /**< bit 1 SDIS: soft disconnect                 */
    uint32_t crs_cr;   /**< bits 31:24 TRIM, bit 6 AUTOTRIMEN, 5 CEN    */
    uint32_t crs_isr;  /**< bits 31:16 FECAP, 15 FEDIR, 1 SYNCOKF, ...  */
    uint32_t ungated;  /**< how many times a gated PHY clock was found  */
};

extern volatile Snapshot snapshot;

/** Trim HSI48 against the host's start-of-frame.
 *
 *  Call after `hw.Init()` -- which is what turns HSI48 on and points the USB
 *  clock at it -- and before USB is started. Harmless if the oscillator was
 *  fine: CRS then trims by nothing.
 *
 *  Note the sync source. On the H750 the peripheral libDaisy calls `USB_OTG_FS`
 *  -- the Seed's own micro-USB, `MidiUsbTransport::Config::INTERNAL` -- is
 *  *USB2*, not USB1. `USB2_OTG_FS_PERIPH_BASE` is 0x40080000 and USB1 is the
 *  HS core at 0x40040000. Syncing to USB1 would compile, run, and never lock.
 */
void EnableCrs();

/** Ungate the PHY clock if a suspend gated it, and refresh `snapshot`.
 *  Main loop; costs a handful of register reads. */
void Service();

} // namespace usbdiag

#endif
