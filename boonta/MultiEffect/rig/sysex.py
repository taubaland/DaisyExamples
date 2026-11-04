"""Ask the pedal to describe itself, and print what it says.

The editor in `boonta-multieffect-editor` has always been a transcription: its
`js/midi.js` restates `MidiMap.h` and its `js/params.js` restates the parameter
ranges, and nothing could tell you when one had drifted from the other. Once
slots became pluggable that stopped being merely untidy -- the registry is now
build-dependent, since `ID_RNBO` exists only in builds with a patch compiled
in, so there is no static answer for anyone to transcribe.

This is the other end of `Sysex.cpp`. It is the check that the pedal's answer
is well formed before a browser depends on it, and it doubles as the readable
description of what a build actually contains:

    make sysex

Needs USB, not the DIN input: the reply goes out over USB only, because the TRS
jack on this pedal is an input and the UART transport blocks while it sends.
"""

import os
import sys
import time

import mido

MANUFACTURER = 0x7D  # reserved by the MIDI spec for non-commercial use
DEVICE = 0x42

REQ_IDENTITY = 0x01
REQ_CATALOGUE = 0x02
RSP_IDENTITY = 0x41
RSP_EFFECT = 0x42

PORT_MATCH = os.environ.get("BOONTA_MIDI_PORT", "Daisy")

# Long enough for the main loop to notice and answer, short enough that a dead
# link is obvious rather than a hang. The pedal sends one message per pass.
REPLY_TIMEOUT = 2.0


def find_port(names, what):
    for name in names:
        if PORT_MATCH.lower() in name.lower():
            return name
    print(f"no MIDI {what} matching {PORT_MATCH!r}")
    print(f"  available: {names}")
    print("  set BOONTA_MIDI_PORT to match yours")
    return None


def get_string(data, at):
    """Length-prefixed string. Returns (text, next index)."""
    if at >= len(data):
        return "<overrun>", at
    length = data[at]
    at += 1
    text = "".join(chr(c) for c in data[at : at + length])
    return text, at + length


def collect(inport, deadline):
    """Every SysEx of ours that arrives before the deadline."""
    out = []
    while time.time() < deadline:
        for msg in inport.iter_pending():
            if msg.type != "sysex":
                continue
            d = list(msg.data)
            if len(d) >= 3 and d[0] == MANUFACTURER and d[1] == DEVICE:
                out.append(d)
        time.sleep(0.01)
    return out


def check(ok, what, detail=""):
    print(f"{'  ok' if ok else 'FAIL':>6} {what} {detail}")
    return 0 if ok else 1


def main():
    out_name = find_port(mido.get_output_names(), "output")
    in_name = find_port(mido.get_input_names(), "input")
    if not out_name or not in_name:
        return 2

    print(f"out: {out_name}\nin:  {in_name}\n")

    failures = 0
    with mido.open_output(out_name) as port, mido.open_input(in_name) as inport:
        # --- identity ---------------------------------------------------
        list(inport.iter_pending())  # drop anything already queued
        port.send(mido.Message("sysex", data=[MANUFACTURER, DEVICE, REQ_IDENTITY]))
        replies = collect(inport, time.time() + REPLY_TIMEOUT)

        failures += check(len(replies) == 1, "identity: exactly one reply",
                          f"(got {len(replies)})")
        if not replies:
            print("\nnothing came back. The pedal answers over USB only, and only "
                  "from the main loop -- check it is running and on USB.")
            return 1

        d = replies[0]
        failures += check(d[2] == RSP_IDENTITY, "identity: is an identity reply")
        proto, effect_count, slots, knobs, presets = d[3:8]
        print(f"       protocol {proto}, {effect_count} effects, "
              f"{slots} slots, {knobs} knobs, {presets} presets\n")

        # --- catalogue ---------------------------------------------------
        port.send(mido.Message("sysex", data=[MANUFACTURER, DEVICE, REQ_CATALOGUE]))
        # One message per main-loop pass, so allow time proportional to how
        # many are coming rather than a flat wait.
        replies = collect(inport, time.time() + REPLY_TIMEOUT + 0.2 * effect_count)

        effects = [r for r in replies if r[2] == RSP_EFFECT]
        failures += check(
            len(effects) == effect_count,
            "catalogue: one message per effect",
            f"(identity said {effect_count}, got {len(effects)})",
        )

        seen_ids = []
        for e in effects:
            at = 3
            eid = e[at]
            at += 1
            r, g, b = e[at], e[at + 1], e[at + 2]
            at += 3
            name, at = get_string(e, at)
            toggle, at = get_string(e, at)
            params = []
            for _ in range(knobs):
                text, at = get_string(e, at)
                params.append(text)

            seen_ids.append(eid)
            print(f"  id {eid}  {name:<10} rgb({r:>3},{g:>3},{b:>3})  "
                  f"toggle: {toggle}")
            print(f"            {' | '.join(params)}")

            failures += check(at == len(e), f"catalogue: id {eid} fully consumed",
                              f"({at} of {len(e)})")

        failures += check(seen_ids == sorted(seen_ids), "catalogue: ids in order",
                          str(seen_ids))
        failures += check(len(set(seen_ids)) == len(seen_ids),
                          "catalogue: no duplicate ids")

    print(f"\n{'all passed' if not failures else str(failures) + ' failing'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
