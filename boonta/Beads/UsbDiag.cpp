#include "UsbDiag.h"

#include "daisy_boonta.h"

namespace usbdiag
{
volatile Snapshot snapshot = {};

/** The OTG core's power and clock gating register.
 *
 *  Reached by address rather than through a PCD handle, because the handle
 *  that owns this peripheral lives inside libDaisy's `usbd_conf.c` and is only
 *  reachable once USB has been initialised -- while the register itself is
 *  what we want to watch across that boundary.
 *
 *  `USB_OTG_FS` on the H750 is the *second* OTG core; see the header.
 */
static volatile uint32_t* const kPcgcctl = reinterpret_cast<volatile uint32_t*>(
    USB2_OTG_FS_PERIPH_BASE + USB_OTG_PCGCCTL_BASE);

static USB_OTG_GlobalTypeDef* const kGlobal
    = reinterpret_cast<USB_OTG_GlobalTypeDef*>(USB2_OTG_FS_PERIPH_BASE);

static volatile uint32_t* const kDevice = reinterpret_cast<volatile uint32_t*>(
    USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE);

static constexpr uint32_t kDctlIndex = 1; /**< DCFG, DCTL, DSTS are the first three */
static constexpr uint32_t kDstsIndex = 2;

void EnableCrs()
{
    __HAL_RCC_CRS_CLK_ENABLE();

    RCC_CRSInitTypeDef cfg = {};

    cfg.Prescaler = RCC_CRS_SYNC_DIV1;

    // The Seed's micro-USB is the OTG_FS core, which is USB2 on this part.
    cfg.Source   = RCC_CRS_SYNC_SOURCE_USB2;
    cfg.Polarity = RCC_CRS_SYNC_POLARITY_RISING;

    // Start-of-frame arrives at 1 kHz, so one sync period is 48000 cycles of a
    // correct HSI48. The macro subtracts the one the counter needs.
    cfg.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);

    cfg.ErrorLimitValue        = RCC_CRS_ERRORLIMIT_DEFAULT;
    cfg.HSI48CalibrationValue  = RCC_CRS_HSI48CALIBRATION_DEFAULT;

    HAL_RCCEx_CRSConfig(&cfg);
}

void Service()
{
    // Reading a peripheral whose clock is off is a bus fault, not a zero. USB
    // is brought up in MidiControl::Init() before the main loop starts, so in
    // practice this is always true -- but "in practice" and "the init order
    // never changes" are different claims, and one of them faults.
    if(!(RCC->AHB1ENR & RCC_AHB1ENR_USB2OTGFSEN))
        return;

    // The suspend callback gates this and nothing ever ungates it. Clearing
    // STOPCLK is what brings the data path back; see the header.
    if(*kPcgcctl & USB_OTG_PCGCCTL_STOPCLK)
    {
        *kPcgcctl &= ~static_cast<uint32_t>(USB_OTG_PCGCCTL_STOPCLK);
        snapshot.ungated++;
    }

    snapshot.pcgcctl = *kPcgcctl;
    snapshot.gahbcfg = kGlobal->GAHBCFG;
    snapshot.gintsts = kGlobal->GINTSTS;
    snapshot.dctl    = kDevice[kDctlIndex];
    snapshot.dsts    = kDevice[kDstsIndex];
    snapshot.crs_cr  = CRS->CR;
    snapshot.crs_isr = CRS->ISR;
}

} // namespace usbdiag
