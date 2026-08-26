"""Does the pedal really remember its settings across a power cycle?

Writes a distinctive state, waits for the debounced save, resets the MCU, and
checks what comes back. A `monitor reset` restarts the processor without
touching QSPI, which is exactly the part of a power cycle that matters here -
the flash contents are what is under test, not the power rail.
"""

import subprocess
import time

import pedal

SETTLE_S = 3.5  # Storage waits 2 s of quiet before writing

# Deliberately unlike the defaults, and unlike anything the pots would produce.
MARK_EQ = [0.11, 0.22, 0.33, 0.44, 0.55, 0.66]
MARK_META = [0.77, 0.12, 0.34, 0.56, 0.78, 0.9]


def reset_mcu():
    subprocess.run(
        [pedal.GDB, pedal.ELF, "-batch",
         "-ex", "target extended-remote :3333",
         "-ex", "monitor reset run",
         "-ex", "detach"],
        capture_output=True, text=True, timeout=90)


def show(label, d):
    print(f"  {label}")
    print(f"    page {int(d['PAGE'][0])}  bypass {int(d['BYPASS'][0])}  "
          f"slots {[int(v) for v in d['SLOTS']]}")
    print(f"    EQ   {['%.2f' % v for v in d['EQ']]}")
    print(f"    META {['%.2f' % v for v in d['META']]}")


print("writing a distinctive state...")
pedal.setup(bypass=False, eq_out=False, drive_out=True, reverb_out=False,
            eq=MARK_EQ, meta=MARK_META)
# Park the page too, so the restore has something unambiguous to prove.
pedal._run(["set var state.page_ = 2", "set var state.order_ = 4"])

before = pedal.read()
show("before reset:", before)

print(f"\nwaiting {SETTLE_S}s for the debounced save to fire...")
time.sleep(SETTLE_S)

print("resetting the MCU (QSPI untouched, as in a power cycle)...")
reset_mcu()
time.sleep(2.0)

after = pedal.read()
print()
show("after reset:", after)

ok = True
for key in ("EQ", "META"):
    for i, (a, b) in enumerate(zip(before[key], after[key])):
        if abs(a - b) > 0.02:
            print(f"\n  MISMATCH {key}[{i}]: {a:.3f} -> {b:.3f}")
            ok = False
for key in ("PAGE", "SLOTS", "BYPASS"):
    if [int(v) for v in before[key]] != [int(v) for v in after[key]]:
        print(f"\n  MISMATCH {key}: {before[key]} -> {after[key]}")
        ok = False

print("\nPASS: settings survived the reset." if ok else
      "\nFAIL: settings did not come back.")
