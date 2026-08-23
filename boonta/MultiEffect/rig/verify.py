"""Regression check for the relay logic: engaged must feed the codec, bypassed
must cut it while still passing dry to the output.

This is the test that caught the inverted bypass relay. Run it after any change
to LedView::DriveRelays.

Relays are driven only through the model - the firmware decides what to write -
because that is the behaviour under test. Interleaved so playing dynamics
cancel; play steadily throughout.
"""

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 1.2
REPS = 4
OUT_CH = rig.PEDAL_OUT_CH


def read_probe():
    """Peak levels either side of the DSP. Needs a PROBE_IO build."""
    out = pedal._run([
        'printf "PROBE %.6f %.6f %.6f %.6f\\n", probe.in_l, probe.in_r, '
        'probe.out_l, probe.out_r',
    ])
    for line in out.splitlines():
        p = line.split()
        if p and p[0] == "PROBE":
            return [float(x) for x in p[1:]]
    raise RuntimeError(
        "no probe symbol - build with C_DEFS += -DPROBE_IO and reflash:\n" + out)


def capture(n_in):
    return sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR, device=rig.IN_DEVICE,
                  channels=n_in, dtype="float32", blocking=True)


STATES = [
    ("ENGAGED  (bypass off)", dict(pedal.NEUTRAL)),
    ("BYPASSED (bypass on) ", dict(pedal.NEUTRAL, bypass=True)),
]

n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]
res = {name: {"in": [], "out": []} for name, _ in STATES}

print(f"KEEP PLAYING - {REPS} interleaved rounds\n")
for rep in range(REPS):
    for name, st in STATES:
        pedal.setup(**st)
        read_probe()  # flush the decaying peak hold
        rec = capture(n_in)
        p = read_probe()
        res[name]["in"].append(max(p[0], p[1]))
        res[name]["out"].append(rig.peak(rec[:, OUT_CH]))
    print(f"  round {rep + 1}")


def med_db(name, key):
    return rig.db(float(np.median(res[name][key])))


print(f"\n{'state':24} {'codec in':>10} {'pedal out':>10}")
print("-" * 48)
for name, _ in STATES:
    print(f"{name:24} {med_db(name, 'in'):10.1f} {med_db(name, 'out'):10.1f}")

eng, byp = STATES[0][0], STATES[1][0]
d_in = med_db(eng, "in") - med_db(byp, "in")
d_out = med_db(eng, "out") - med_db(byp, "out")

print(f"\nengaged vs bypassed at the codec : {d_in:+.1f} dB")
print(f"engaged vs bypassed at the output: {d_out:+.1f} dB")

if d_in > 15:
    print("\nPASS: engaging the pedal feeds the codec; bypassing cuts it.")
else:
    print("\nFAIL: bypass state barely changes what reaches the codec.")
    print("      Check the relay polarity in LedView::DriveRelays - all three")
    print("      relays should follow `engage`, bypass included.")
