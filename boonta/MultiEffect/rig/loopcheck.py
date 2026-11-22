"""Is something outside the pedal returning its output to its input?

Run this before investigating any complaint about how the pedal sounds. A
monitoring path that loops the pedal back into itself imitates three DSP bugs at
once, convincingly:

  - the pedal never sounds fully wet, because recirculated dry signal is present
    at its *input* and is therefore inside the wet signal, where no mix control
    can reach it
  - the drive appears to self-oscillate before it affects anything, because it
    does: its gain takes the loop past unity
  - the reverb sounds terrible, because it is inside a feedback loop

The test switches every effect out, which reduces the chain to
`out = in * trim * level` - pure feedforward, no state, no feedback, and
mathematically incapable of oscillating. Anything it does beyond scaling its
input by those two gains is coming from outside the pedal.

Works whether or not a source is playing: rather than looking for "loud", it
checks that the output tracks the gains *proportionally*, which is the one thing
a feedforward path must do and a loop will not.
"""

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 1.0
REPS = 2
OUT_CH = rig.PEDAL_OUT_CH

#: How far above its predicted level a reading has to sit before the path is
#: doing something a feedforward chain cannot. Generous, because the prediction
#: assumes a steady source and a guitar is not one.
TOLERANCE_DB = 10.0

IN_GAINS = (0.25, 0.5, 0.75, 1.0)
OUT_LEVELS = (0.2, 0.4, 0.6, 0.8)


def trim_db(knob):
    """Chain.cpp: InputTrim() is 4^((knob-0.5)*2), so +/-12 dB about centre."""
    return (knob - 0.5) * 2.0 * 20.0 * np.log10(4.0)


def level_db(knob):
    """Chain.cpp: OutputLevel() is 4*knob^2."""
    return 20.0 * np.log10(max(4.0 * knob * knob, 1e-9))


n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]


def capture():
    rec = sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR, device=rig.IN_DEVICE,
                 channels=n_in, dtype="float32", blocking=True)
    return rig.peak(rec[:, OUT_CH])


def measure(in_gain, out_level):
    """All three effects switched out: the chain is pure feedforward."""
    vals = []
    for _ in range(REPS):
        pedal.setup(**dict(pedal.NEUTRAL,
                           eq_out=True, drive_out=True, reverb_out=True,
                           meta=[1, 1, in_gain, 1, out_level, 1.0]))
        vals.append(capture())
    return rig.db(float(np.median(vals)))


print("Switching every effect out and sweeping the two gains.\n")

# Reference: the quietest corner, where a loop is least likely to be running.
ref_in, ref_out = IN_GAINS[0], OUT_LEVELS[0]
ref = measure(ref_in, ref_out)
ref_gain = trim_db(ref_in) + level_db(ref_out)
print(f"reference: in {ref_in}, out {ref_out} -> {ref:.1f} dBFS\n")

print(f"{'in':>5} {'out':>6} {'measured':>10} {'predicted':>10} {'excess':>8}")
print("-" * 46)

worst = -99.0
worst_at = None
for ing in IN_GAINS:
    for outl in OUT_LEVELS:
        got = measure(ing, outl)
        predicted = ref + (trim_db(ing) + level_db(outl)) - ref_gain
        excess = got - predicted
        if excess > worst:
            worst, worst_at = excess, (ing, outl)
        flag = "  <==" if excess > TOLERANCE_DB else ""
        print(f"{ing:5.2f} {outl:6.2f} {got:10.1f} {predicted:10.1f} {excess:+8.1f}{flag}")

print()
if worst > TOLERANCE_DB:
    print(f"VERDICT: at in={worst_at[0]}, out={worst_at[1]} the output is "
          f"{worst:.0f} dB above")
    print("         what these gains can produce. A dry feedforward path cannot")
    print("         do that, so something outside the pedal is returning its")
    print("         output to its input - check monitoring on the interface, and")
    print("         whether the pedal is patched across both its ins and outs.")
    print()
    print("         Break the loop before trusting any judgement of how the")
    print("         effects sound. Every measurement here is contaminated.")
else:
    print("VERDICT: the output tracks both gains as a feedforward path should.")
    print("         No feedback loop. Complaints about the sound are worth")
    print("         chasing in the DSP.")
