"""Does the DSP audibly do what it claims? Interleaved A/B at the pedal output.

Three pairs, each differing in exactly one thing, measured as band energy so a
guitar works as the stimulus even though its level wanders.
"""

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 1.2
REPS = 3
OUT_CH = rig.PEDAL_OUT_CH


def band_db(x, lo, hi):
    """Energy in a band, via FFT."""
    w = np.hanning(len(x))
    spec = np.abs(np.fft.rfft(x * w))
    freqs = np.fft.rfftfreq(len(x), 1 / rig.SR)
    sel = (freqs >= lo) & (freqs < hi)
    return rig.db(np.sqrt(np.sum(spec[sel] ** 2)) / len(x))


def capture():
    n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]
    rec = sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR, device=rig.IN_DEVICE,
                 channels=n_in, dtype="float32", blocking=True)
    return rec[:, OUT_CH]


TESTS = [
    ("EQ low shelf",
     dict(pedal.NEUTRAL, eq=[0.6, 0.5, 0.5, 0.0, 0.5, 0.5]),   # low CUT
     dict(pedal.NEUTRAL, eq=[0.6, 0.5, 0.5, 1.0, 0.5, 0.5]),   # low BOOST
     (60, 250), "low band"),
    ("EQ high shelf",
     dict(pedal.NEUTRAL, eq=[0.5, 0.5, 0.4, 0.5, 0.5, 0.0]),   # high CUT
     dict(pedal.NEUTRAL, eq=[0.5, 0.5, 0.4, 0.5, 0.5, 1.0]),   # high BOOST
     (3000, 10000), "high band"),
    ("Drive in/out",
     dict(pedal.NEUTRAL, drive_out=True),
     dict(pedal.NEUTRAL, drive_out=False,
          drive=[0.9, 0.7, 0.3, 0.5, 0.5, 1.0]),
     (1500, 8000), "harmonics"),
]

print(f"KEEP PLAYING - {len(TESTS)} A/B pairs, {REPS} rounds each\n")

for name, state_a, state_b, band, band_name in TESTS:
    a_vals, b_vals = [], []
    for _ in range(REPS):
        pedal.setup(**state_a)
        a_vals.append(band_db(capture(), *band))
        pedal.setup(**state_b)
        b_vals.append(band_db(capture(), *band))

    a = float(np.median(a_vals))
    b = float(np.median(b_vals))
    print(f"{name:16} {band_name:10} {band[0]:5}-{band[1]:5} Hz   "
          f"A {a:7.1f}   B {b:7.1f}   delta {b - a:+6.1f} dB")

print("\nExpected: low shelf and high shelf should each move their own band by")
print("a large positive delta; drive should add energy in the harmonic band.")
