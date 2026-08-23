"""Listen: what is arriving where, with no test tone involved.

Records all 16 Push inputs and reads the pedal's codec probe, so one run shows
both ends at once:
  - which Push input the guitar is on (proves guitar + Push input + capture)
  - whether anything is reaching the pedal's codec
"""

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 6.0
OUT_CH = rig.PEDAL_OUT_CH


def read_probe():
    out = pedal._run([
        'printf "PROBE %.6f %.6f %.6f %.6f\\n", probe.in_l, probe.in_r, '
        'probe.out_l, probe.out_r',
    ])
    for line in out.splitlines():
        p = line.split()
        if p and p[0] == "PROBE":
            return [float(x) for x in p[1:]]
    return None


n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]
print(f"recording {n_in} Push inputs for {SECONDS:.0f}s - play now\n")

rec = sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR, device=rig.IN_DEVICE,
             channels=n_in, dtype="float32", blocking=True)

probe = read_probe()

# Push inputs 5-8 carry its own internal buses at a constant level whatever
# is plugged in, so they are excluded from "where is the signal" - they always
# win otherwise.
IGNORE = {5, 6, 7, 8}

print(f"{'ch':>3}  {'peak dBFS':>10}  {'rms dBFS':>9}   bar")
loud = []
for c in range(n_in):
    x = rec[:, c]
    pk = rig.db(rig.peak(x))
    rm = rig.db(rig.rms(x))
    if (c + 1) not in IGNORE:
        loud.append((pk, c + 1))
    bar = "#" * max(0, int((pk + 80) / 4))
    print(f"{c+1:3}  {pk:10.1f}  {rm:9.1f}   {bar}")

loud.sort(reverse=True)
print(f"\nloudest Push inputs: " +
      ", ".join(f"ch{c} {p:.1f} dBFS" for p, c in loud[:3]))

if probe:
    print(f"\npedal codec: in {probe[0]:.5f}/{probe[1]:.5f}   out {probe[2]:.5f}/{probe[3]:.5f}")
    print(f"             in peak {rig.db(max(probe[0], probe[1])):.1f} dBFS "
          f"(noise floor is about -61)")
    if max(probe[0], probe[1]) > 0.004:
        print("             -> signal IS reaching the pedal")
    else:
        print("             -> nothing reaching the pedal")

print()
if loud[0][0] > -50:
    print(f"Signal is arriving at the PC on input {loud[0][1]} ({loud[0][0]:.1f} dBFS).")
else:
    print("Nothing on any usable input - no signal is reaching the PC.")
