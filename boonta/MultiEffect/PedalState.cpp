#include "PedalState.h"

/** The six ways three effects can be ordered. Row = order index, column =
 *  position in the chain. Kept here rather than in the header so the table is
 *  a private detail of SlotAt(). */
static constexpr PedalState::Slot kChainOrder[PedalState::kOrderCount]
                                             [PedalState::SLOT_LAST] = {
    {PedalState::SLOT_EQ, PedalState::SLOT_DRIVE, PedalState::SLOT_REVERB},
    {PedalState::SLOT_EQ, PedalState::SLOT_REVERB, PedalState::SLOT_DRIVE},
    {PedalState::SLOT_DRIVE, PedalState::SLOT_EQ, PedalState::SLOT_REVERB},
    {PedalState::SLOT_DRIVE, PedalState::SLOT_REVERB, PedalState::SLOT_EQ},
    {PedalState::SLOT_REVERB, PedalState::SLOT_EQ, PedalState::SLOT_DRIVE},
    {PedalState::SLOT_REVERB, PedalState::SLOT_DRIVE, PedalState::SLOT_EQ},
};

/** Which meta knob carries which slot's amount. The three are interleaved with
 *  the gain knobs rather than grouped, so this is a table and not arithmetic --
 *  reorder the meta page and only this changes. */
static constexpr PedalState::MetaParam kSlotAmountKnob[PedalState::SLOT_LAST] = {
    PedalState::META_EQ_AMOUNT,     // SLOT_EQ
    PedalState::META_DRIVE_AMOUNT,  // SLOT_DRIVE
    PedalState::META_REVERB_AMOUNT, // SLOT_REVERB
};

/** PageSlot() leans on the first three pages being the three slots. */
static_assert(static_cast<int>(PedalState::PAGE_EQ)
                      == static_cast<int>(PedalState::SLOT_EQ)
                  && static_cast<int>(PedalState::PAGE_DRIVE)
                         == static_cast<int>(PedalState::SLOT_DRIVE)
                  && static_cast<int>(PedalState::PAGE_REVERB)
                         == static_cast<int>(PedalState::SLOT_REVERB),
              "the first three pages must line up with the three slots");

/** Where each page sits before you have ever turned a knob on it.
 *
 *  These matter more than they would on a single-page pedal. Soft pickup means
 *  an unvisited page holds these values while it is being *heard*, so every
 *  default has to be somewhere musically safe: the EQ flat, the reverb present
 *  but not swamping, every slot fully in circuit. The page that is on screen at
 *  boot is the exception -- Controls arms it immediately, so the pots win.
 */
static constexpr float kDefault[PedalState::PAGE_LAST][PedalState::kKnobCount]
    = {
        // low f  mid f  high f  low g  mid g  high g
        {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f},
        // gain   tone  char   bias  level   mix
        {0.35f, 0.5f, 0.25f, 0.5f, 0.5f, 1.0f},
        // time  damp  predly  diff  lowcut  mix
        {0.5f, 0.5f, 0.0f, 0.7f, 0.15f, 0.3f},
        // eq amt  rvb amt  in gain  drv amt  out lvl  mix
        {1.0f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f},
};

void PedalState::Reset()
{
    for(int p = 0; p < PAGE_LAST; p++)
        for(int k = 0; k < kKnobCount; k++)
            param_[p][k] = kDefault[p][k];

    for(int i = 0; i < TOGGLE_LAST; i++)
        toggle_[i] = POS_MID;

    for(int i = 0; i < SLOT_LAST; i++)
        slot_bypassed_[i] = false; // the master bypass already boots us out

    expression_     = 0.f;
    page_           = PAGE_EQ;
    order_          = 0; // EQ -> Drive -> Reverb
    bypassed_       = true; // boot up out of circuit
    page_edited_    = false; // nothing has been changed on the boot page yet
    pickup_pending_ = 0;
}

void PedalState::NextPage()
{
    page_ = static_cast<Page>((page_ + 1) % PAGE_LAST);
}

void PedalState::NextOrder()
{
    order_ = (order_ + 1) % kOrderCount;
}

void PedalState::PrevOrder()
{
    order_ = (order_ + kOrderCount - 1) % kOrderCount;
}

void PedalState::SetOrder(int order)
{
    order_ = ((order % kOrderCount) + kOrderCount) % kOrderCount;
}

PedalState::Slot PedalState::SlotAt(int position) const
{
    return kChainOrder[order_][position];
}

float PedalState::SlotAmount(Slot s) const
{
    return param_[PAGE_META][kSlotAmountKnob[s]];
}

PedalState::Slot PedalState::PageSlot(Page page)
{
    return static_cast<int>(page) < static_cast<int>(SLOT_LAST)
               ? static_cast<Slot>(page)
               : SLOT_LAST;
}
