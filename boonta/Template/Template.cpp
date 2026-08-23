// # Boonta Template
//
// Starting point for a Boonta effect, split along MVC lines:
//
//   PedalState  (model)      what the pedal is set to. No hardware.
//   Controls    (controller) knobs/switches/expression -> model.
//   LedView     (view)       model -> LEDs and bypass relays.
//   Effect      (dsp)        model -> samples.
//   Template.cpp             wiring, and nothing else.
//
// To build an effect, edit Effect.cpp. To re-map a knob, edit the table at the
// top of Controls.cpp. To change what the LEDs say, edit LedView.cpp. This file
// should rarely need to change at all.
//
// ## Controls
// | Control  | Function                                                |
// |----------|---------------------------------------------------------|
// | KNOB_1-6 | Drive, Tone, Level, Mix, Time, Feedback                  |
// | TOG_SW_1 | Range: down = low gain, centre = mid, up = high          |
// | TOG_SW_2 | Character (unused by the stock effect)                   |
// | TOG_SW_3 | Routing (unused by the stock effect)                     |
// | SW_SEL_1 | Previous preset                                         |
// | SW_SEL_2 | Next preset                                             |
// | SW_FS_1  | Bypass (latching, drives the relays)                    |
// | SW_FS_2  | Alt: momentary boost, and tap tempo                     |
// | EXP      | Read into the model; unassigned by default              |

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
