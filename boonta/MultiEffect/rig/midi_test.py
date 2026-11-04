"""Send MIDI at the pedal and check the model moved.

Needs no audio at all: the CCs go out over MIDI, the result is read back off the
ST-Link. That makes this the most direct test in the rig - it exercises exactly
the path a controller would use and checks the parameter it was aimed at.

Requires the Daisy's own USB port connected to this machine (the ST-Link cable
is a different port and does not carry MIDI), or a UART MIDI source.
"""

import sys
import time

import mido

import pedal

# Mirrors MidiMap.h. Kept as literals rather than parsed from the header so a
# silent drift between the two shows up as a failing test.
EQ_BASE, DRIVE_BASE, REVERB_BASE, META_BASE = 20, 26, 102, 108
MASTER_BYPASS, SLOT_BASE, CHAIN_ORDER, PAGE_SELECT = 114, 115, 118, 119

PAGE_NAMES = ["EQ", "DRIVE", "REVERB", "META"]


def find_port():
    names = mido.get_output_names()
    print("MIDI output ports:")
    for n in names:
        print(f"   {n}")
    for n in names:
        if any(k in n.lower() for k in ("daisy", "boonta", "stm32")):
            return n
    return None


def accepted():
    out = pedal._run(['printf "ACC %u\\n", midi.accepted_'])
    for line in out.splitlines():
        if line.startswith("ACC "):
            return int(line.split()[1])
    return None


port_name = sys.argv[1] if len(sys.argv) > 1 else find_port()
if not port_name:
    print("\nNo Daisy MIDI port found. Connect the Daisy's own USB port to this")
    print("machine, or pass a port name: python midi_test.py \"Your Port\"")
    raise SystemExit(1)

print(f"\nusing: {port_name}\n")
port = mido.open_output(port_name)

before = accepted()
print(f"messages accepted before: {before}")

# A picked-up knob overwrites a MIDI change on the next audio block - the hand
# beats the controller. Park them so the map itself is what is under test.
pedal.park_current_page()
print("knobs parked so remote changes stick\n")

failures = 0


def send_cc(cc, value):
    port.send(mido.Message("control_change", control=cc, value=value, channel=0))
    time.sleep(0.15)


def check(label, ok, detail=""):
    global failures
    print(f"{'  ok  ' if ok else '  FAIL'} {label} {detail}")
    if not ok:
        failures += 1


# --- parameters -----------------------------------------------------------
for base, page in ((EQ_BASE, 0), (DRIVE_BASE, 1), (REVERB_BASE, 2), (META_BASE, 3)):
    send_cc(base + 2, 127)
    send_cc(base + 3, 0)
    d = pedal.read()
    key = ["EQ", None, None, "META"][page] if page in (0, 3) else None
    # read() only exposes EQ and META; check those directly and the rest by
    # reading the raw page array.
    out = pedal._run([f'printf "P %.3f %.3f\\n", state.param_[{page}][2], state.param_[{page}][3]'])
    vals = [float(x) for x in next(
        (l.split()[1:] for l in out.splitlines() if l.startswith("P ")), ["9", "9"])]
    check(f"{PAGE_NAMES[page]:6} knob3=127 knob4=0",
          abs(vals[0] - 1.0) < 0.02 and abs(vals[1]) < 0.02,
          f"-> {vals[0]:.2f} / {vals[1]:.2f}")

# --- switches -------------------------------------------------------------
send_cc(MASTER_BYPASS, 127)
check("master bypass CC 127 = in circuit", pedal.read()["BYPASS"][0] == 0)
send_cc(MASTER_BYPASS, 0)
check("master bypass CC 0 = bypassed", pedal.read()["BYPASS"][0] == 1)

send_cc(SLOT_BASE + 1, 0)
check("drive slot CC 0 = switched out", pedal.read()["SLOTS"][1] == 1)
send_cc(SLOT_BASE + 1, 127)
check("drive slot CC 127 = switched in", pedal.read()["SLOTS"][1] == 0)

for value, expect in ((0, 0), (127, 5)):
    send_cc(CHAIN_ORDER, value)
    out = pedal._run(['printf "O %d\\n", state.order_'])
    got = int(next((l.split()[1] for l in out.splitlines() if l.startswith("O ")), -1))
    check(f"chain order CC {value} -> {expect}", got == expect, f"got {got}")

for value, expect in ((0, 0), (127, 3)):
    send_cc(PAGE_SELECT, value)
    got = int(pedal.read()["PAGE"][0])
    check(f"page select CC {value} -> {PAGE_NAMES[expect]}", got == expect, f"got {got}")

# --- presets --------------------------------------------------------------
port.send(mido.Message("program_change", program=5, channel=0))
time.sleep(0.6)
out = pedal._run(['printf "PR %d\\n", storage.current_'])
got = int(next((l.split()[1] for l in out.splitlines() if l.startswith("PR ")), -1))
check("program change 5 recalls preset 5", got == 5, f"got {got}")

port.send(mido.Message("program_change", program=0, channel=0))
time.sleep(0.6)
out = pedal._run(['printf "PR %d\\n", storage.current_'])
got = int(next((l.split()[1] for l in out.splitlines() if l.startswith("PR ")), -1))
check("program change 0 returns to preset 0", got == 0, f"got {got}")

# --- unmapped controllers must be ignored ---------------------------------
mid = accepted()
for cc in (1, 7, 11, 64, 74):
    send_cc(cc, 100)
check("unmapped controllers are ignored", accepted() == mid,
      f"(accepted counter unchanged at {mid})")

after = accepted()
print(f"\nmessages accepted: {before} -> {after}")
print("all passed" if failures == 0 else f"{failures} FAILING")
