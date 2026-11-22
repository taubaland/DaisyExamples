// # Boonta Effect Skeleton (DaisyDuino)
//
// The Arduino build of boonta/NewEffect. Same MVC split, same model, same
// pass-through DSP -- only the board layer differs:
//
//   PedalState  (model)      what the pedal is set to. No hardware.   [shared]
//   Effect      (dsp)        model -> samples.  <- write this one     [shared]
//   BoontaDuino (board)      pins, switches, LED driver, relays.      [Arduino]
//   Controls    (controller) knobs/switches/expression -> model.
//   LedView     (view)       model -> LEDs and bypass relays.
//   NewEffectDuino.ino       wiring, and nothing else.
//
// PedalState and Effect are byte-identical to the libDaisy build: they never
// included libDaisy in the first place. Controls and LedView differ only in
// which board class they talk to. See PORTING.md.
//
// Board: Daisy Seed, via the STM32 core's GenH7 entry.
//   FQBN  STMicroelectronics:stm32:GenH7:pnum=DAISY_SEED

#include "DaisyDuino.h"

#include "BoontaDuino.h"
#include "Controls.h"
#include "Effect.h"
#include "LedView.h"
#include "PedalState.h"

static BoontaDuino hw;

static PedalState state;
static Controls   controls;
static LedView    view;
static Effect     effect;

// Audio-callback context: read the inputs, then render the audio. Both are
// deterministic and allocation free. The LEDs and relays are deliberately not
// touched here -- see loop().
static void AudioCallback(float **in, float **out, size_t size)
{
    controls.Process();
    effect.Process(state, in, out, size);
}

void setup()
{
    // DAISY.init() returns a DaisyHardware for the official boards. The Boonta
    // is not one of them, so the return is ignored and BoontaDuino owns the
    // pedal's own controls; this call is here for the codec and audio engine.
    DAISY.init(DAISY_SEED, AUDIO_SR_48K);

    hw.Init(DAISY.AudioCallbackRate());

    controls.Init(&hw, &state);
    view.Init(&hw);
    effect.Init(DAISY.get_samplerate());

    DAISY.begin(AudioCallback);
}

void loop()
{
    // Main-loop context: anything slow, blocking or mechanical. The LED
    // transfer is a blocking Wire write and the relays are physical switches,
    // so neither belongs in the audio callback.
    view.Update(state);
    delay(1);
}
