"""Measure an EQ band's gain at the stimulus frequency.

A single sustained note only tells you about the frequencies it contains.
Everything else in the spectrum is noise and reverb tail 40-70 dB down, and
reading a filter curve off that produces confident nonsense - an early version
of this script drew a "shelf" that was +11 dB flat across every band including
40 Hz, because it was measuring the noise floor moving.

So: find the note, tune the band under test onto it, and measure only there
with a Goertzel. That is one point of the curve, but it is a real one.

For a full curve you need a broadband stimulus - noise or a sweep into the
pedal's input. This is the measurement that works with whatever you are playing.
"""

import sys

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 1.2
REPS = 3
OUT_CH = rig.PEDAL_OUT_CH

# Knob-to-frequency sweeps, mirroring EqEffect.cpp.
SWEEPS = {"low": (40.0, 500.0), "mid": (200.0, 4000.0), "high": (1500.0, 12000.0)}
#: Which knob index carries each band's frequency and gain, from PedalState.
KNOBS = {"low": (0, 3), "mid": (1, 4), "high": (2, 5)}


def knob_for(band, hz):
    lo, hi = SWEEPS[band]
    hz = max(lo, min(hi, hz))
    return float(np.log(hz / lo) / np.log(hi / lo))


def capture(n_in):
    rec = sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR, device=rig.IN_DEVICE,
                 channels=n_in, dtype="float32", blocking=True)
    return rec[:, OUT_CH]


def dominant(x):
    w = np.hanning(len(x))
    spec = np.abs(np.fft.rfft(x * w))
    freqs = np.fft.rfftfreq(len(x), 1 / rig.SR)
    keep = (freqs > 40) & (freqs < 12000)
    idx = np.argmax(np.where(keep, spec, 0))
    return float(freqs[idx])


band = sys.argv[1] if len(sys.argv) > 1 else "mid"
if band not in SWEEPS:
    raise SystemExit(f"band must be one of {list(SWEEPS)}")

n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]

print("finding the note - keep playing...")
pedal.setup(**pedal.NEUTRAL)
f0 = dominant(capture(n_in))
fk = knob_for(band, f0)
lo, hi = SWEEPS[band]
placed = lo * (hi / lo) ** fk
print(f"  fundamental {f0:.0f} Hz; {band} band tuned to {placed:.0f} Hz "
      f"(knob {fk:.3f})\n")

if abs(placed - f0) / f0 > 0.25:
    print(f"  NOTE: the {band} band cannot reach {f0:.0f} Hz - its sweep is "
          f"{lo:.0f}-{hi:.0f} Hz.")
    print("        The reading below understates what the band can do.\n")

fq, gq = KNOBS[band]


def eq_with(gain):
    """The EQ alone: drive and reverb switched out.

    The reverb especially has to go. Its tail runs for seconds, so switching
    from boost to cut leaves the previous, louder state still decaying into the
    capture. That puts a floor under the cut reading - measured -7.3 dB instead
    of the rated -15 - while leaving the boost looking fine, so it reads as an
    asymmetric EQ rather than as contamination.
    """
    eq = [0.5] * 6
    eq[fq] = fk
    eq[gq] = gain
    return dict(pedal.NEUTRAL, eq=eq, drive_out=True, reverb_out=True)


levels = {}
for label, gain in (("full cut", 0.0), ("flat", 0.5), ("full boost", 1.0)):
    vals = []
    for _ in range(REPS):
        pedal.setup(**eq_with(gain))
        vals.append(rig.goertzel(capture(n_in), f0))
    levels[label] = rig.db(float(np.median(vals)))
    print(f"  {label:11} {levels[label]:8.1f} dBFS @ {f0:.0f} Hz")

cut = levels["full cut"] - levels["flat"]
boost = levels["full boost"] - levels["flat"]

print(f"\ncut vs flat  : {cut:+.1f} dB")
print(f"boost vs flat: {boost:+.1f} dB")
print(f"full sweep   : {levels['full boost'] - levels['full cut']:+.1f} dB")
print("\nRated +/-15 dB per band, so a full sweep should approach 30 dB with the")
print("band sitting on the note.")
