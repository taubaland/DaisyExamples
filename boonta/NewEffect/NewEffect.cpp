// # Boonta Effect Skeleton
//
// A Boonta effect with everything but the effect. The board is brought up, the
// controls are read into a model and the LEDs and relays follow it -- the DSP
// is a pass-through, waiting to be written.
//
// Split along MVC lines:
//
//   PedalState  (model)      what the pedal is set to. No hardware.
//   Controls    (controller) knobs/switches/expression -> model.
//   LedView     (view)       model -> LEDs and bypass relays.
//   Effect      (dsp)        model -> samples.  <- write this one
//   NewEffect.cpp            wiring, and nothing else.
//
// Start in Effect.cpp. To re-map a knob, edit the table at the top of
// Controls.cpp. To change what the LEDs say, edit LedView.cpp. This file
// should rarely need to change at all.
//
// ## What the controls are wired to
//
// These are the meanings the model carries. Nothing interprets them yet --
// that is Effect.cpp's job -- so re-point anything that doesn't suit.
//
// | Control  | Lands in the model as                                   |
// |----------|---------------------------------------------------------|
// | KNOB_1-6 | PARAM_DRIVE, TONE, LEVEL, MIX, TIME, FEEDBACK (0..1)    |
// | TOG_SW_1 | TOGGLE_RANGE     (POS_LOW / POS_MID / POS_HIGH)         |
// | TOG_SW_2 | TOGGLE_CHARACTER (POS_LOW / POS_MID / POS_HIGH)         |
// | TOG_SW_3 | TOGGLE_ROUTING   (POS_LOW / POS_MID / POS_HIGH)         |
// | SW_SEL_1 | Previous preset                                         |
// | SW_SEL_2 | Next preset                                             |
// | SW_FS_1  | Bypass (latching, drives the relays)                    |
// | SW_FS_2  | Alt held, and a tapped tempo in ms                      |
// | EXP      | Expression; unassigned by default (kExpressionTarget)   |

#include "daisy_boonta.h"

#include "Controls.h"
#include "Effect.h"
#include "LedView.h"
#include "PedalState.h"

using namespace daisy;

static DaisyBoonta hw;

static PedalState state;
static Controls   controls;
static LedView    view;
static Effect     effect;

// Audio-callback context: read the inputs, then render the audio. Both are
// deterministic and allocation free. The LEDs and relays are deliberately not
// touched here -- see the main loop.
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    controls.Process();
    effect.Process(state, in, out, size);
}

int main(void)
{
    hw.Init();

    controls.Init(&hw, &state);
    view.Init(&hw);
    effect.Init(hw.AudioSampleRate());

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    // Main-loop context: anything slow, blocking or mechanical. The LED driver
    // transfer is I2C DMA and the relays are physical switches, so neither
    // belongs in the audio callback.
    while(1)
    {
        view.Update(state);
        System::Delay(1);
    }
}
