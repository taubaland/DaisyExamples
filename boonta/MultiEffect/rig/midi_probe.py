"""Which MIDI port, if any, actually reaches the pedal?

Sends a distinctive CC out of every available port in turn and watches the
pedal's accepted-message counter over the ST-Link. Whichever port makes the
counter move is the one wired to it - useful when the route runs through
another device's DIN output and you cannot be sure it is passing anything on.
"""

import time

import mido

import pedal

CC = 20      # EQ low freq - harmless, and visible in the model
VALUE = 100


def counter():
    """(uart received, usb received, accepted).

    Received counts everything that arrived, mapped or not, so an unmapped
    controller still proves the link is alive. Watching only `accepted` cannot
    tell "nothing arrived" from "arrived but we do not claim that number"."""
    out = pedal._run(['printf "ACC %u %u %u\\n", midi.received_uart_, '
                      'midi.received_usb_, midi.accepted_'])
    for line in out.splitlines():
        if line.startswith("ACC "):
            return tuple(int(x) for x in line.split()[1:])
    raise RuntimeError("no counters - is this a MIDI build?\n" + out)


def eq0():
    out = pedal._run(['printf "EQ0 %.3f\\n", state.param_[0][0]'])
    for line in out.splitlines():
        if line.startswith("EQ0 "):
            return float(line.split()[1])
    return None


ports = mido.get_output_names()
print("probing every MIDI output for a route to the pedal\n")

base = counter()
print(f"accepted counter starts at {base}\n")

found = []
for name in ports:
    try:
        with mido.open_output(name) as port:
            for _ in range(4):
                port.send(mido.Message("control_change",
                                       control=CC, value=VALUE, channel=0))
                time.sleep(0.05)
    except Exception as e:
        print(f"  {name[:44]:44} could not open: {str(e)[:30]}")
        continue

    time.sleep(0.35)
    now = counter()
    d = tuple(n - b for n, b in zip(now, base))
    base = now
    mark = "  <== REACHES THE PEDAL" if (d[0] or d[1]) else ""
    print(f"  {name[:44]:44} uart+{d[0]} usb+{d[1]} accepted+{d[2]}{mark}")
    if d[0] or d[1]:
        found.append(name)

print()
if found:
    print("routes that work:")
    for f in found:
        print(f"  {f}")
    print(f"\nEQ knob 1 now reads {eq0():.3f} (expected {VALUE / 127:.3f})")
else:
    print("No port reached the pedal.")
    print("USB: the Daisy's own port must enumerate as a MIDI device.")
    print("DIN: the sending device has to pass this through to its MIDI output.")
