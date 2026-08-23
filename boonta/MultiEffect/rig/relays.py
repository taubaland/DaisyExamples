"""Relay sweep measuring BOTH ends, with a guitar actually in the pedal.

For each relay combination: what the codec sees, and what comes out of the
pedal into Push. This separates three things that have been tangled all along:

  codec silent, Push loud   -> audio routes around the Daisy (analog bypass)
  codec loud,   Push loud   -> the wanted state: through the DSP
  codec loud,   Push silent -> gets in, does not get out

Push inputs 5-8 carry constant internal Push audio and are ignored.
"""

import numpy as np
import sounddevice as sd

import pedal
import rig

SECONDS = 1.8
IGNORE = {5, 6, 7, 8}  # Push's own internal buses, always present


def read_probe():
    out = pedal._run([
        'printf "PROBE %.6f %.6f %.6f %.6f\\n", probe.in_l, probe.in_r, '
        'probe.out_l, probe.out_r',
    ])
    for line in out.splitlines():
        p = line.split()
        if p and p[0] == "PROBE":
            return [float(x) for x in p[1:]]
    return [0, 0, 0, 0]


def set_relays(i, o, b):
    pedal._run([
        f"call hw.relay_input.Write({i})",
        f"call hw.relay_output.Write({o})",
        f"call hw.relay_bypass.Write({b})",
    ])


n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]
print("KEEP PLAYING throughout - eight combinations, about a minute.\n")
print(f"{'in':>2} {'out':>4} {'byp':>4} | {'codec in':>9} | {'push ch1':>9} {'push ch2':>9} "
      f"{'push ch3':>9} {'push ch4':>9}")
print("-" * 74)

rows = []
for i in (1, 0):
    for o in (1, 0):
        for b in (0, 1):
            set_relays(i, o, b)
            read_probe()  # flush the decaying hold

            rec = sd.rec(int(rig.SR * SECONDS), samplerate=rig.SR,
                         device=rig.IN_DEVICE, channels=n_in,
                         dtype="float32", blocking=True)
            p = read_probe()

            codec = rig.db(max(p[0], p[1]))
            ch = [rig.db(rig.peak(rec[:, c])) for c in range(4)]
            rows.append((codec, max(ch[:2]), i, o, b))
            print(f"{i:2} {o:4} {b:4} | {codec:9.1f} | {ch[0]:9.1f} {ch[1]:9.1f} "
                  f"{ch[2]:9.1f} {ch[3]:9.1f}")

print()
best_codec = max(rows)
print(f"loudest at the codec: in={best_codec[2]} out={best_codec[3]} "
      f"bypass={best_codec[4]} -> {best_codec[0]:.1f} dBFS")

codec_spread = max(r[0] for r in rows) - min(r[0] for r in rows)
print(f"codec spread across the eight: {codec_spread:.1f} dB")

if max(r[0] for r in rows) < -50:
    print("\nVERDICT: no relay state gets the guitar into the codec. The input jack")
    print("         never reaches the Daisy - analog front end or wiring.")
else:
    print(f"\nVERDICT: in={best_codec[2]} out={best_codec[3]} bypass={best_codec[4]} "
          "routes signal through the Daisy.")
    print("         If that differs from what the firmware drives, the relay")
    print("         polarity in LedView is inverted.")

pedal._run(["set var view.relays_known_ = 0"])
